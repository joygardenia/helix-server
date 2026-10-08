## Why

当前 HTTP 解析、路由、数据库、缓存和服务发现全部集中在 `main.cc`，并在 Reactor 的 IO 线程回调里同步执行；一个慢 DB 调用（等连接、行锁、慢查询）会阻塞整个 subLoop 上的所有连接。同时 HTTP 处理只是 `onMessage` 里一个 11 行的字符串切分，没有协议消息边界语义。需要在保留自研网络库的前提下，拆出协议层与业务层，并把阻塞业务移出 IO 线程。

## What Changes

- 新增自研轻量 HTTP 模块（`include/http/`、`src/http/`）：`HttpContext` 增量状态机（请求行 → 头部 → 请求体 → 完成）、`HttpRequest`、`HttpResponse`，复用项目现有 `Buffer`，不引入 muduo 或其他外部框架。
- 新增通用业务线程池 `ThreadPool`（固定 worker + 有界任务队列 + 条件变量），承载所有阻塞业务。
- 改造 `main.cc` 的 `onMessage`：解析请求后投递到业务线程池，业务完成后经所属 `EventLoop::runInLoop` 回到 IO 线程执行 `conn->send`。
- 明确分层依赖方向：网络层（`TcpServer`/`TcpConnection` 等）与 HTTP 层不得依赖 `DBPool`/`LFU`/`etcd`，这些只出现在业务层。
- 响应跨线程生命周期与“只发送一次”语义由连接持有者保证。

## Capabilities

### New Capabilities

- `http-protocol`: 基于 `Buffer` 的增量 HTTP 请求解析、请求/响应对象、`Content-Length` 消息边界、半包/粘包/顺序 keep-alive 与协议错误响应。
- `business-thread-pool`: 固定 worker 的业务线程池，有界任务队列、队列满时的明确拒绝、任务完成后回投 IO 线程。
- `layered-dispatch`: IO 线程完成“解析 → 投递”，业务层独占 DB/LFU/etcd，业务结果经 `runInLoop` 回投的调度与隔离规则。

### Modified Capabilities

- 无

## Impact

- 新增 `include/http/`、`src/http/` 模块与 `ThreadPool` 组件，并调整 `src/CMakeLists.txt`。
- 影响 `src/main.cc`（`onMessage`、路由与业务组装）、`include/TcpConnection.h`/`src/TcpConnection.cc`（跨线程 send 与响应生命周期）。
- `DBPool`、`LFU`、`EtcdClient`、`ConsistentHash` 的调用点从 IO 线程迁移到业务线程池内，依赖关系不变。
- 保持现有 HTTP 路由与响应行为，现有压测入口与界面兼容。
