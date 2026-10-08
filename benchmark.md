# 压测数据记录

## 测试环境
- CPU: 13th Gen Intel Core i5-13500H，12物理核，16线程
- 服务器线程模型: One Loop per Thread（多 Reactor 多线程）
- 测试工具: wrk
- 监听端口: 8080

---

## 测试一：不同并发数下的 QPS（固定 3 线程）

```
wrk -t4 -c100 -d10s --latency http://127.0.0.1:8080/
wrk -t4 -c500 -d10s --latency http://127.0.0.1:8080/
wrk -t4 -c1000 -d10s --latency http://127.0.0.1:8080/
```

### c100 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.18ms  730.35us  34.42ms   92.30%
    Req/Sec    21.54k     3.27k   56.47k    78.55%
  Latency Distribution
     50%    1.04ms
     75%    1.18ms
     90%    1.81ms
     99%    2.44ms
  858935 requests in 10.10s, 54.88MB read
Requests/sec:  85043.00
Transfer/sec:      5.43MB
```

### c500 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     5.95ms    2.90ms 103.99ms   92.11%
    Req/Sec    21.48k     2.58k   25.67k    69.44%
  Latency Distribution
     50%    5.27ms
     75%    5.85ms
     90%    7.63ms
     99%   11.88ms
  846338 requests in 10.01s, 54.08MB read
Requests/sec:  84507.33
Transfer/sec:      5.40MB
```

### c1000 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    12.11ms    3.82ms 122.04ms   91.35%
    Req/Sec    20.90k     2.72k   25.07k    70.45%
  Latency Distribution
     50%   11.20ms
     75%   12.08ms
     90%   15.06ms
     99%   23.28ms
  823061 requests in 10.05s, 52.59MB read
Requests/sec:  81924.08
Transfer/sec:      5.23MB
```

### 汇总

| 并发数 | QPS       | P50延迟  | P99延迟  |
|--------|-----------|---------|---------|
| 100    | 85,043    | 1.04ms  | 2.44ms  |
| 500    | 84,507    | 5.27ms  | 11.88ms |
| 1000   | 81,924    | 11.20ms | 23.28ms |

**结论**：并发从 100 增长至 1000，QPS 稳定在 82,000~85,000，降幅不足 4%，全程零错误连接。
延迟随并发线性增长，P99 从 2.44ms 升至 23.28ms，是已识别的优化方向。

---

## 测试二：不同线程数下的 QPS（固定 c500）

```
wrk -t4 -c500 -d10s --latency http://127.0.0.1:8080/
```

### 1线程 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     9.99ms    3.69ms 115.08ms   95.94%
    Req/Sec    12.79k     1.76k   13.89k    91.16%
  Latency Distribution
     50%    9.21ms
     75%    9.41ms
     90%   10.99ms
     99%   21.51ms
  503927 requests in 10.04s, 32.20MB read
Requests/sec:  50206.19
Transfer/sec:      3.21MB
```

### 2线程 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     6.58ms    3.32ms 105.10ms   96.94%
    Req/Sec    19.53k     2.13k   22.59k    91.41%
  Latency Distribution
     50%    6.16ms
     75%    6.31ms
     90%    7.18ms
     99%   12.28ms
  769601 requests in 10.04s, 49.17MB read
Requests/sec:  76657.90
Transfer/sec:      4.90MB
```

### 4线程 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     6.71ms    3.89ms 167.95ms   98.15%
    Req/Sec    19.18k     2.00k   23.59k    73.99%
  Latency Distribution
     50%    5.74ms
     75%    8.23ms
     90%    8.71ms
     99%   11.51ms
  755370 requests in 10.04s, 48.27MB read
Requests/sec:  75262.97
Transfer/sec:      4.81MB
```

### 8线程 原始输出
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     7.22ms    5.35ms 202.68ms   99.38%
    Req/Sec    18.06k     1.39k   20.27k    88.13%
  Latency Distribution
     50%    6.89ms
     75%    7.43ms
     90%    7.99ms
     99%   10.37ms
  711559 requests in 10.02s, 45.47MB read
Requests/sec:  70989.26
Transfer/sec:      4.54MB
```

### 汇总

| 线程数 | QPS       | P99延迟  |
|--------|-----------|---------|
| 1      | 50,206    | 21.51ms |
| 2      | 76,658    | 12.28ms |
| 4      | 75,263    | 11.51ms |
| 8      | 70,989    | 10.37ms |

**结论**：线程数从 1 扩展至 2，QPS 从 50,206 提升至 76,658（提升约 52%），P99 延迟从 21.51ms 降至 12.28ms。
4线程以上 QPS 趋于饱和，分析原因为请求处理过轻（固定响应），瓶颈转移至内核网络栈，符合 Amdahl 定律。

---

## 测试三：内存池对比（3线程）

### 测试条件
- malloc 版本：`new TcpConnection`（系统分配）
- 内存池版本：`newElement<TcpConnection>`（HashBucket 内存池）
- 服务器线程数：3，wrk 4线程

### malloc 原始输出

#### c100
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.18ms  373.50us  13.81ms   88.52%
    Req/Sec    21.25k     2.74k   25.83k    67.50%
  Latency Distribution
     50%    1.05ms
     75%    1.20ms
     90%    1.76ms
     99%    2.45ms
  845910 requests in 10.03s, 54.05MB read
Requests/sec:  84371.00
Transfer/sec:      5.39MB
```

#### c500
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     5.84ms    2.30ms 113.06ms   92.24%
    Req/Sec    21.73k     2.38k   25.97k    62.12%
  Latency Distribution
     50%    5.24ms
     75%    5.82ms
     90%    7.01ms
     99%   11.01ms
  855941 requests in 10.03s, 54.69MB read
Requests/sec:  85336.68
Transfer/sec:      5.45MB
```

### 内存池原始输出

#### c100
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.78ms    0.89ms  37.09ms   84.83%
    Req/Sec    14.23k     2.44k   23.01k    71.75%
  Latency Distribution
     50%    1.54ms
     75%    1.86ms
     90%    2.89ms
     99%    3.43ms
  566147 requests in 10.04s, 36.17MB read
Requests/sec:  56392.90
Transfer/sec:      3.60MB
```

#### c500
```
Running 10s test @ http://127.0.0.1:8080/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     8.81ms    3.96ms 117.24ms   85.56%
    Req/Sec    14.43k     1.72k   18.62k    69.95%
  Latency Distribution
     50%    7.71ms
     75%    8.72ms
     90%   14.20ms
     99%   16.06ms
  568511 requests in 10.02s, 36.33MB read
Requests/sec:  56741.67
Transfer/sec:      3.63MB
```

### 汇总

| 并发数 | malloc QPS | 内存池 QPS | QPS 变化 | P99 malloc | P99 内存池 |
|--------|-----------|-----------|---------|-----------|-----------|
| 100    | 84,371    | 56,393    | -33%    | 2.45ms    | 3.43ms    |
| 500    | 85,337    | 56,742    | -34%    | 11.01ms   | 16.06ms   |

### 结论
内存池接入 `TcpConnection` 后 QPS 下降约 33%，为负优化。

**原因一：TcpConnection 分配频率极低**
keep-alive 下每个连接只分配一次，500 并发 10 秒内只有 500 次 new/delete，但处理了 85 万次请求，分配频率与请求数不在同一量级。

**原因二：perf 证明瓶颈不在堆分配**
对 malloc 版本压测期间做 perf 采样，关键数据：
- `write` 系统调用（内核网络栈）：**25.56%** CPU
- `malloc` / `free` 相关符号：**0%**，完全未出现

CPU 时间全花在 `write` 系统调用上，堆分配开销可忽略不计。内存池没有任何收益可以抵消其锁竞争的额外开销，导致 QPS 下降。

---

### 深入分析：为什么内存池在这里是负优化

**keep-alive 下 TcpConnection 的分配频率极低：**

wrk 跑 10 秒，500 并发，实际只有 500 次 `new` 和 500 次 `delete`，但期间处理了 85 万次请求。分配次数与请求数完全不在一个量级。

**内存池的锁竞争抵消了收益：**

内存池每次 `allocate` / `deallocate` 都要加 mutex：
```cpp
std::lock_guard<std::mutex> lock(mutexForFreeList_);
```
多线程并发下各线程争抢同一把锁，开销反而高于 malloc。现代 Linux 的 ptmalloc 每个线程有独立 arena，线程间基本不竞争，在低频分配场景下比带锁的内存池更快。

**内存池收益公式：**
```
收益 = 分配频率 × 单次节省时间 - 锁竞争开销
```
分配频率低时结果为负，内存池本身的存在（初始化占内存、锁操作、间接寻址）就是负担。

**内存池真正适用的前提：**
1. 对象分配频率足够高（请求级，而非连接级）
2. 对象大小相对固定，能归入固定槽位
3. 锁竞争可控（理想情况用 thread-local 内存池彻底规避锁）

**适合接入内存池的典型场景：**
- 每次请求解析时 new/delete 的临时对象
- 链表式 Buffer 的节点（每次读写都在 new/delete）
- 游戏服务器每帧创建销毁的子弹、特效对象

本项目 Buffer 基于 `vector` 实现，`TcpConnection` 与连接绑定，整体分配频率低，不满足内存池的适用前提。

---

---

## 待办：内存池与缓存的真正接入

### 内存池（方案二）
接入条件：`onMessage` 中存在请求级高频 new/delete 的对象。
当前 `onMessage` 只返回固定字符串，无高频分配，暂无合适接入点。
后续若加入 HTTP 解析、请求对象构造等逻辑，优先考虑接入。

### LFU 缓存
接入条件：`onMessage` 中有静态文件读取逻辑（读 HTML/图片等）。
当前无文件读取，缓存完全没有接入点。
接入方式：命中缓存直接返回，不命中读磁盘后 `put` 进缓存。

两者未接入的根本原因：`onMessage` 实现过于简单（固定响应），既无磁盘 I/O 也无高频分配，是后续扩展的前置条件。

**并发稳定性**：
> 并发从 100 增长至 1000，QPS 稳定在 82,000~85,000，降幅不足 4%，P99 延迟从 2.44ms 升至 23.28ms，全程零错误连接

**水平扩展能力**：
> 线程数从 1 扩展至 2，QPS 从 50,206 提升至 76,658（提升约 52%），P99 延迟从 21.51ms 降至 12.28ms；分析发现瓶颈在内核网络栈而非用户态计算，线程数超过 2 后 QPS 趋于饱和，符合 Amdahl 定律

---

## 2026-10 复测：请求级高频对象（Timer）接入内存池

### 背景

连接级对象（TcpConnection）接入内存池实测为负优化后，按"分配频率需为请求级"的结论，选取每请求分配一次的 Timer（连接超时定时器）做对照实验。

### 实验设计

- 改造点：`TimerQueue.cc` 中 `new Timer` / `delete Timer` 共 3 处替换为 `memoryPool::newElement` / `deleteElement`
- 前置：`src/CMakeLists.txt` 为 `src_lib` 补链 `memory_lib`
- 频率量化：每次请求触发 `resetIdleTimer` → `runAfter` → `addTimer`，25 万 QPS 下每秒约 25 万次 Timer 分配
- 压测：wrk -t4 -c1000 -d10s，GET /items（DB 未启动，固定 500 响应）

### 数据

| 版本 | QPS（两轮） | Latency avg / max |
|------|------------|-------------------|
| malloc（基线） | 245,883 / 266,948 | 4.12ms / 67.0ms、3.88ms / 84.3ms |
| memoryPool | 268,012 / 262,120 | 3.75ms / 44.0ms、3.84ms / 45.7ms |

均值：malloc 256,416 vs memoryPool 265,066（+3.4%），轮间抖动 ±8%，**无显著差异**。

### 结论：全局锁内存池在小对象高频场景依然无法胜出

1. glibc tcache（per-thread cache）对 ≤1032B 小对象为无锁 pop/push，Timer（约 64B）正中靶心，malloc 已近零成本
2. 各 subLoop 在各自 TimerQueue 分配/释放 Timer（线程本地操作），却须抢全局 `HashBucket` 的共享 mutex，锁竞争抵消池化收益
3. 深层归因：分配频率 ×（是否加锁）× tcache 机制 三者共同决定池化收益；线程本地（thread-local）内存池才可能在无锁前提下胜出，本项目无此实现

### 与本轮环境基线的关联

- 本轮环境（新机器）epoll 固定响应基线约 25~27 万 QPS，为历史文档 8.4 万（旧环境）的 3 倍，量化结果不具跨环境可比性，仅相对对比有效
- `USE_IOURING=1`（POLL_ADD_MULTI）复测：c1000 下 QPS 低于 epoll 约 12~17%（与历史 -15% 方向一致）；低并发（c100/c500）无差异；wrk 阉割版无百分位输出，P99 结论未复测
- 内存池实验代码已回退，仓库保持 malloc 版

---

## 2026-10 复测：网络层与内存池全量 wrk 数据（新机器）

### 测试环境

- 机器：Manjaro，内核 6.12.103，cmake O 等级默认
- wrk：`f8eb608`（阉割版，**无 Latency Distribution 百分位输出**，仅 Thread Stats avg/std/max）
- 命令：`wrk -t4 -c{N} -d10s http://127.0.0.1:18085/items`
- 请求路径：GET /items（DB 未启动 → `db pool not init` 固定 500 响应，零 DB/磁盘 IO）
- 服务器：默认 epoll；`USE_IOURING=1` 切 io_uring（POLL_ADD_MULTI）
- 日志：AsyncLogging 全量 INFO 落盘（两版本同等负担）

### A. epoll 基线（malloc 版，全部轮次）

| 并发 | 轮次 | QPS | Latency avg | max |
|------|------|-----|-------------|-----|
| c100 | 轮1 / 轮2 | 252,646 / 277,536 | — | — |
| c500 | 轮1 / 轮2 | 290,393 / 244,920 | — | — |
| c1000 | 轮1 | 269,364 | — | — |
| c1000 | 轮2（完整输出） | 253,472 | 3.97ms | 40.48ms |
| c1000 | Timer 基线 / 复测 | 245,883 / 266,948 | 4.12 / 3.88ms | 67.0 / 84.3ms |

均值口径：c100≈265k、c500≈268k、c1000≈245~269k → **并发 100→1000 QPS 波动 <6%，横向平稳**。

### B. io_uring（POLL_ADD_MULTI）

| 并发 | QPS | Latency avg | max | vs epoll |
|------|-----|-------------|-----|----------|
| c100 | 264,663 | — | — | -0.2%（无差异） |
| c500 | 263,614 | — | — | -1.5% |
| c1000 | 222,974 / 226,388 | 3.53ms | 64.60ms | **-12%~-17%** |

结论：POLL_ADD 仍是事件通知而非异步 IO，高并发下 QPS 不敌 epoll（与原文档 -15% 方向一致）；avg 延迟略优、max 抖动更大；未拿到百分位，P99 结论未复测。

### C. TcpConnection 内存池对照（新环境复测）

| 并发 | malloc（两轮） | memoryPool（两轮） | 均值差异 |
|------|---------------|--------------------|----------|
| c100 | 252,646 / 277,536 | 269,152 / 282,887 | +4.1% |
| c500 | 290,393 / 244,920 | 287,637 / 260,121 | +2.3% |

结论：连接级分配场景两版本差异在轮间噪声（±8%）内，**无正收益也无负优化**——原文档"内存池接入连接对象 QPS -33%"在新环境无法复现，历史数据不再采信。

### D. Timer 内存池对照

见上节"2026-10 复测：请求级高频对象（Timer）接入内存池"：请求级高频场景同样无显著差异（+3.4%，噪声内），归因 glibc tcache 无锁分配 + 全局锁池竞争。

### 汇总结论

1. 当前环境网络层吞吐上限由**内核网络栈 + 系统调用往返**主导（约 26 万 QPS/1000 并发），事件分发与连接生命周期未成瓶颈
2. 小对象分配（连接级与请求级）均非瓶颈，tcache 已覆盖，池化无正收益
3. 本环境量化结果与历史文档（8.4 万 QPS、P99 23ms、-33%）**不具跨环境可比性**，历史数据仅作方向参考
4. wrk 阉割版无百分位，尾延迟（P99/P99.9）数据缺失，需 wrk2 或服务端埋点补测

---

## 2026-10 wrk2 延迟分布复测（P99 补充）

### wrk2 构建

- 版本 4.0.0，源码 `github.com/giltene/wrk2`，已构建于 `/tmp/wrk2/wrk`
- 踩坑：LuaJIT 2.0 生成的 `bytecode.o` 与 binutils 2.47 不兼容（ld/ld.lld/objcopy 均报 `string table [3] is corrupt`），解法为用 `ld.gold -r` 合并该对象后再链接
- 用法差异：wrk2 固定速率模型，**必须指定 `-R` 目标吞吐**；`-L` 输出 HdrHistogram 百分位
- 注意：`-R` 超过服务器实际能力时队列饱和、延迟虚高（客户端排队而非服务端真实延迟），需在能力内测

### 数据（epoll 默认，c1000，-L，端口 /items 固定响应）

| 百分位 | -R200k | -R150k |
|--------|--------|--------|
| 50% | 3.85ms | 3.69ms |
| 75% | 19.84ms | 15.27ms |
| 90% | 42.91ms | 36.70ms |
| 99% | 91.52ms | 93.38ms |
| 99.9% | 122.56ms | 130.37ms |
| 99.99% | 147.84ms | — |
| max | 162.82ms | 172.54ms |

（-R250k 超能力，P50 即 222ms 的队列堆积数据已丢弃）

### 解读

1. **P50 3.7~3.9ms 与 wrk1（尽力而为）avg 3.97ms 吻合** → 服务端中位延迟可信，与 26 万 QPS 自洽（Little's Law: 1000/260000 ≈ 3.85ms）
2. **长尾严重**：P75 即 15~20ms，P99 ≈90ms，max 170ms
3. **主嫌疑：全量 INFO 日志落盘**（AsyncLogging 每事件落盘、无级别过滤，25 万 QPS 下磁盘 flush 停顿形成尾部）——待优化项，未处理
4. 次嫌疑：wrk2 客户端 1000 连接固定速率校准抖动

---

## 测试六：HTTP 分层 + 业务线程池（add-http-layer-and-business-pool）

### 变更点
- 引入 `HttpContext` 增量解析（替换 `parseRequest` 单次切分）
- 业务经 `ThreadPool`（8 worker，有界队列 1024）执行，完成后 `runInLoop` 回投 IO 线程发送
- 路由经 `Router` 注册；DB/LFU/etcd 全部移入业务 Handler

### 环境与参数
- 服务器: `./bin/main node1 8080 127.0.0.1 127.0.0.1`（3 IO 线程 + 8 业务 worker）
- 数据库: MariaDB 12.3.2 @127.0.0.1:3306，`game_db`，`item_defs` 2 行
- etcd: 未启动（注册失败不影响 HTTP/DB 路由）
- 工具: `wrk -t4 -d10s --latency http://127.0.0.1:8080/items`
- 说明: `/items` 首请求查库，命中 LFU 缓存后直接返回缓存 body（后续请求走缓存）

### 数据

| 并发 | Requests/sec | Transfer/sec | Avg | P50 | P75 | P90 | P99 | Max |
|------|-------------|--------------|-----|-----|-----|-----|-----|-----|
| c100 | 148842 | 32.93 MB | 682.92us | 498us | 1.03ms | 1.12ms | 2.04ms | 6.91ms |
| c500 | 144319 | 31.93 MB | 3.47ms | 2.66ms | 5.24ms | 5.84ms | 8.24ms | 85.76ms |
| c1000 | 138833 | 30.72 MB | 7.18ms | 5.44ms | 10.19ms | 12.42ms | 15.99ms | 80.76ms |

### 功能验证（同一构建）
- `/items` 200、`POST /players` 200、`GET /players/1` 200、`POST /inventory/use` 200
- `/discover?key=abc` 200、`/stats` 200、未知路径 404
- 粘包（一次读两请求 → 两响应 200）、畸形请求行 400、缺 `Content-Length` 411
- `ctest`: http_test / threadpool_test / loopback_test 3/3 通过

### 解读 / 限制
1. 单机 `/items` 命中缓存场景约 14 万 QPS，与 IO 线程能力同量级；业务池未成为瓶颈（`/items` 命中缓存后 Handler 极轻）。
2. 本组数据**不能**证明「业务池对阻塞 DB 的收益」——需对照慢查询/行锁场景（`FOR UPDATE`、长事务）才有意义，属后续工作。
3. 队列满返回 503（保持连接），未在压测中触发（8 worker 未饱和）。
4. 该构建为 Debug，未开优化；数据仅作基线，不代表生产峰值。

---

## 测试七：慢查询对照（同步 IO 线程 vs 业务线程池异步）

### 目的
量化「阻塞业务移出 IO 线程」的收益：当某请求的业务因 DB 锁而阻塞时，
共享同一 subLoop 的其他连接是否被拖住。

### 对照构建
- 异步版（当前实现）：`bin/main`，8 业务 worker + 3 IO 线程
- 同步对照版：`/tmp/opencode/syncbuild`，将 `ThreadPool::submit` 改为在调用线程内联执行
  （业务在 IO 线程直跑，等价改造前行为），其余代码完全相同

### 阻塞来源
外部 MySQL 会话对 `inventory(player_id=1,item_id=1)` 执行
`BEGIN; SELECT ... FOR UPDATE; SELECT SLEEP(3); COMMIT;` 持锁 3 秒。
锁请求 `/inventory/use` 会因 `FOR UPDATE` 排队等待；旁观请求 `/players/1` 走普通
`SELECT`（不涉及该行锁），用于观察是否被拖累。

### 数据（持锁 3s，1 个锁请求 + 40 个旁观请求）

| 版本 | 旁观 /players/1 p50 | 旁观 p90 | 旁观 max | 锁请求 |
|------|--------------------|---------|---------|--------|
| 同步（IO 线程直跑） | 1.7ms | **2587.9ms** | **2636.2ms** | 2702ms |
| 异步（业务池） | 1.8ms | 2.5ms | **3.2ms** | 2703ms |

### 结论
1. **异步化收益成立**：同步版下，锁请求阻塞其所在 subLoop，导致共享该 subLoop 的
   旁观连接 max 被拖到 ~2.6s；异步版旁观连接 max 仅 3.2ms，慢请求不再拖累 IO 线程上的其他连接。
2. **业务池饱和是新的瓶颈**：当锁请求数（20）超过业务 worker 数（8）时，业务池被占满，
   旁观请求改为在业务池排队（p50 ~2.4s）。故业务 worker 数必须与「并发慢业务数」匹配，
   而非仅匹配 CPU 核数（对应 design Open Question：线程数 vs DBPool 连接数）。
3. 本实验用 `SLEEP(3)` 精确控制阻塞时长，可重复；Debug 构建，绝对值不作生产参考。

### 复现命令
```
# 持锁会话（另开终端）
mysql -h127.0.0.1 -P3306 -utakeaway -ptakeaway123 game_db \
  -e "BEGIN; SELECT qty FROM inventory WHERE player_id=1 AND item_id=1 FOR UPDATE; SELECT SLEEP(3); COMMIT;"
# 旁观者压测
python3 /tmp/opencode/mixed_test.py <port> 1 40
```
