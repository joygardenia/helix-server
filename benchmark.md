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
