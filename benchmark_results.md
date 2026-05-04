# 压测数据记录

## 测试环境
- CPU: 13th Gen Intel Core i5-13500H，12物理核，16线程
- 服务器线程模型: One Loop per Thread（多 Reactor 多线程）
- 测试工具: wrk
- 监听端口: 8080

---

## 测试一：不同并发数下的 QPS（固定 4 线程）

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

## 简历参考写法

**并发稳定性**：
> 并发从 100 增长至 1000，QPS 稳定在 82,000~85,000，降幅不足 4%，P99 延迟从 2.44ms 升至 23.28ms，全程零错误连接

**水平扩展能力**：
> 线程数从 1 扩展至 2，QPS 从 50,206 提升至 76,658（提升约 52%），P99 延迟从 21.51ms 降至 12.28ms；分析发现瓶颈在内核网络栈而非用户态计算，线程数超过 2 后 QPS 趋于饱和，符合 Amdahl 定律
