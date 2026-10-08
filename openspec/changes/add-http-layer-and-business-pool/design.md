## Context

当前服务在 `src/main.cc` 中完成 HTTP 解析（`parseRequest`，一次 `retrieveAllAsString` 后按空格和 `\r\n\r\n` 切分）、路由（if/else）、业务执行（MySQL、LFU、etcd）和响应构造（`httpResp`）。所有业务在 `TcpConnection` 的 `onMessage` 回调里同步执行，即运行在所属 subLoop 的 IO 线程上。

网络库本身（`TcpServer`/`TcpConnection`/`EventLoop` 等）不依赖 DB，但业务逻辑与 IO 线程耦合：一个 DB 阻塞（`DBPool::get()` 等连接、`FOR UPDATE` 行锁、慢查询）会拖住该 subLoop 上的全部连接。同时 `parseRequest` 假设一次读取即完整请求，不具备半包/粘包边界语义。

约束：保留自研网络库与现有 HTTP 路由行为、保持 C++11、不引入 muduo 或其他 Web 框架、尽量减少新增代码与重复实现。

## Goals / Non-Goals

**Goals:**

- 自研增量 HTTP 解析层（`HttpContext`/`HttpRequest`/`HttpResponse`），复用项目 `Buffer`，正确处理半包、粘包、`Content-Length` 边界与顺序 keep-alive。
- 新增通用业务线程池 `ThreadPool`，让所有阻塞业务离开 IO 线程。
- `onMessage` 改为「解析 → 投递业务池 → 业务池经 `EventLoop::runInLoop` 回投 `conn->send`」。
- 明确依赖方向：网络层与 HTTP 层不依赖 `DBPool`/`LFU`/`etcd`/`ConsistentHash`，这些只存在于业务层。
- 保证跨线程响应生命周期安全且「只发送一次」。

**Non-Goals:**

- 不引入 muduo、其他网络/HTTP 框架或外部依赖。
- 不实现 chunked transfer-encoding、HTTP/2、TLS。
- 不重写 Reactor / 收发模型。
- 不改动现有路由的对外语义与响应格式。

## Decisions

### 1. 自研 HttpContext，而不是引入 muduo 的 http 模块

muduo 的 `muduo/net/http` 依赖其 `muduo::net::{TcpServer,Buffer,EventLoop}`，与本项目全局同名类冲突；且其 HttpServer 同样在 IO 线程回调业务，不提供业务线程池。因此自写状态机、复用现有 `Buffer`，仅参考 muduo 的状态机思路。

选择增量状态机而非对完整字符串重扫：TCP 无消息边界，状态机可保留半包状态，避免业务消费不完整请求。状态：`ExpectRequestLine → ExpectHeaders → ExpectBody → GotAll`；解析入口接收 `Buffer*`，用 `peek()/retrieve()` 逐步消费，返回 `Incomplete / Complete / BadRequest`。

### 2. 消息边界用 Content-Length，缺少或不支持则明确报错

第一阶段仅支持 `Content-Length` 界定请求体；无 body 的请求在头部结束即完成。不支持 `Transfer-Encoding: chunked` 时返回 4xx。这足以覆盖现有 GET/POST 路由与 keep-alive 顺序请求。

### 3. 新增独立 ThreadPool，而不是复用 EventLoopThreadPool

`EventLoopThreadPool` 是 IO 事件循环池，每个线程带自己的 epoll；业务任务是阻塞调用，不需要事件循环。因此新增 `ThreadPool`：固定 N 个 worker + `std::queue<Functor>` + `mutex` + `condition_variable`，队列设容量上限，满时按策略拒绝（503）。worker 数量与 `DBPool` 连接数需匹配调优。

### 4. 响应经 runInLoop 回投，连接用 shared_ptr 保活

`TcpConnection::send` 只能在连接所属 IO 线程调用。业务 worker 完成后执行：

```
conn->getLoop()->runInLoop([conn, resp]{ if (conn->connected()) conn->send(resp); });
```

请求投递时携带 `TcpConnectionPtr`（`shared_ptr`），确保异步期间连接对象存活；连接处理中断开时回投回调检测 `connected()` 后丢弃。每个被投递的请求持有一次性发送标记（原子标志），保证响应只发送一次。

### 5. 路由与 Handler 分层，依赖方向单向

`Router` 只做 `(method,path) → handler` 匹配，不依赖 DB。业务 Handler 在业务层实现，由组装代码（`main.cc`）注册。网络层与 HTTP 层头文件不得 include 业务依赖；`DBPool`/`LFU`/`etcd`/`ConsistentHash` 仅出现在业务 Handler 文件与其组装处。

### 6. 复用现有响应构造，消除重复

`httpResp` 统一收敛为 `HttpResponse` 的构造/序列化；`HttpRequest` 统一承担解析结果。`main.cc` 中的 if/else 路由与各 handler 迁出为独立 handler 函数，`main.cc` 只保留依赖组装与启动。

## Risks / Trade-offs

- [跨线程 send 的数据生命周期] → 回投回调按值捕获 `shared_ptr<TcpConnection>` 与响应体；不在业务线程触碰连接内部状态。
- [业务处理期间连接断开] → 回投时检查 `connected()`；断开则丢弃响应，由连接回收链负责清理。
- [线程池队列满] → 有界队列 + 明确拒绝（503），避免无界堆积；拒绝路径不进入 DB。
- [请求体无 Content-Length 或非法] → 返回 400/411/413，不进入业务 Handler，避免状态机卡死。
- [异步化后 `/stats` 埋点语义变化] → 重新定义测点为「业务处理耗时」，并保留端到端记录方式，保证压测口径可解释。
- [线程数 ≠ DB 连接数导致排队] → 默认参数取 `ThreadPool 线程数 == DBPool 连接数`，并以压测验证，而不是预先声称收益。

## Migration Plan

1. 落地 HTTP 模块与单测（不接入业务），验证半包/粘包/keep-alive。
2. 落地 `ThreadPool` 与单测。
3. 用 `HttpContext` 替换 `onMessage` 中的 `parseRequest`，业务仍同步执行，验证行为等价。
4. 引入业务线程池与回投，业务 Handler 全部迁入池内。
5. 分层清理：`DBPool`/`LFU`/`etcd` 调用点只保留在业务层；`main.cc` 收敛为组装。
6. 压测对比异步化前后（同步阻塞 vs 业务池），记录环境与参数。

回滚：HTTP 模块与线程池均为新增模块；如需回退，恢复 `onMessage` 同步路径即可，不影响网络层。

## Open Questions

- 业务线程数与 `DBPool` 连接数的默认值（拟取相等，按核数调优）。
- 队列满时返回 503 后是否关闭连接，还是保留连接允许重试。
- `ThreadPool` 是否需要支持优雅停机时排空队列（与现有 `EventLoop` 退出一致）。
- `/stats` 的 P50/P99 埋点是否重新定义为「业务耗时」并在设计中固定口径。
