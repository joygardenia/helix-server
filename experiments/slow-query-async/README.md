# 慢查询对照实验：同步 IO 线程 vs 业务线程池异步

记录 `add-http-layer-and-business-pool` 变更中「把阻塞业务移出 IO 线程」的收益量化过程。

## 1. 背景与假设

改造前：HTTP 解析、路由、DB/LFU/etcd 全部在 `TcpConnection::onMessage` 里同步执行，
即运行在所属 subLoop 的 IO 线程上。一个阻塞的 DB 调用（等连接、`FOR UPDATE` 行锁、慢查询）
会拖住该 subLoop 上的**所有**连接。

改造后：IO 线程只做「解析 → 投递」，业务在独立 `ThreadPool`（8 worker）执行，
完成后经 `conn->getLoop()->runInLoop(...)` 回投 IO 线程发送。

**待验证假设**：当某请求的业务因 DB 锁阻塞时，共享同一 subLoop 的其他连接不再被拖累。

## 2. 对照构建

| 版本 | 位置 | 行为 |
|------|------|------|
| 异步（当前实现） | 仓库根 `bin/main` | `ThreadPool::submit` 正常投递到 worker |
| 同步对照 | `experiments/slow-query-async/syncbuild/bin/main` | `ThreadPool::submit` 改为在调用线程内联执行，业务在 IO 线程直跑 |

对照改动仅一处（其余代码与主仓库相同）：

```cpp
// syncbuild/src/ThreadPool.cc
bool ThreadPool::submit(Task task) {
  task();          // 直接在调用（IO）线程执行
  return true;
}
```

主仓库 `src/ThreadPool.cc` 的异步实现**未改动**。

## 3. 阻塞来源

外部 MySQL 会话对 `inventory(player_id=1,item_id=1)` 持锁 3 秒：

```sql
BEGIN;
SELECT qty FROM inventory WHERE player_id=1 AND item_id=1 FOR UPDATE;
SELECT SLEEP(3);
COMMIT;
```

- 锁请求 `/inventory/use`：Handler 里 `SELECT ... FOR UPDATE`，会排队等锁。
- 旁观请求 `/players/1`：普通 `SELECT`，不涉及该行锁，用于观察是否被拖累。

## 4. 环境与参数

- 服务器：3 IO 线程（`setThreadNum(3)`）+ 8 业务 worker + 队列 1024
- DB：MariaDB 12.3.2 @127.0.0.1:3306，`game_db`
- 端口：异步 8080 / 同步对照 9090
- 构建：Debug
- 规格：13th Gen Intel Core i5-13500H

## 5. 实验脚本

- `hold_lock.sh <秒>`：起外部会话持锁 N 秒
- `mixed_test.py <port> <锁请求数> <旁观请求数>`：并发发锁请求 + 旁观请求，分别统计延迟
- `block_test.py <port> <N>`：只打 `/items`
- `block_test2.py <port> <N>`：只打 `/inventory/use`

## 6. 结果

### 6.1 空转基线（无锁干扰）

| 版本 | `/items` p50 | `/inventory/use` p50 |
|------|-------------|---------------------|
| 同步 | 0.7ms | 1.6ms |
| 异步 | 0.8ms | 1.7ms |

基线一致，差异来自后续阻塞实验。

### 6.2 核心对照：持锁 3s + **1** 个锁请求 + 40 个旁观请求

| 版本 | 旁观 p50 | 旁观 p90 | 旁观 **max** | 锁请求 |
|------|---------|---------|-------------|--------|
| **同步（IO 直跑）** | 1.7ms | **2587.9ms** | **2636.2ms** | 2702ms |
| **异步（业务池）** | 1.8ms | 2.5ms | **3.2ms** | 2703ms |

**结论**：同步版下，锁请求阻塞其所在 subLoop，共享该 subLoop 的旁观连接 max 被拖到
~2.6s；异步版旁观连接 max 仅 3.2ms，慢请求不再拖累 IO 线程上的其他连接。**假设成立。**

### 6.3 业务池饱和：持锁 3s + **20** 个锁请求 + 40 个旁观请求

| 版本 | 旁观 p50 | 锁请求 p50 |
|------|---------|-----------|
| 同步 | 2436ms | 2647ms |
| 异步 | 2442ms | 2651ms |

**结论**：锁请求数（20）超过业务 worker 数（8）时，业务池被占满，旁观请求改为在
**业务池排队**，异步版不再有优势。这是异步化暴露出的**新瓶颈**。

## 7. 关键洞察

1. **异步化收益成立**：慢请求不再阻塞 IO 线程上的其他连接（6.2）。
2. **业务池线程数是新调优点**：worker 数必须与「并发慢业务数」匹配，而非只匹配 CPU 核数；
   否则慢请求会占满池，让所有请求排队（6.3）。对应 `design.md` Open Question：
   `ThreadPool 线程数 == DBPool 连接数`。
3. 单连接内业务仍串行（`ConnState::processing`），慢请求会阻塞**同一连接**的后续请求，
   但不阻塞**其他连接**——这正是异步化的目标语义。

## 8. 复现步骤

```bash
# 1) 启动异步版
cd <repo> && ./bin/main node1 8080 127.0.0.1 127.0.0.1

# 2) 启动同步对照版
cd experiments/slow-query-async/syncbuild && ./bin/main node1 9090 127.0.0.1 127.0.0.1

# 3) 另开终端持锁 3s，同时压测旁观者
experiments/slow-query-async/hold_lock.sh 3 &
python3 experiments/slow-query-async/mixed_test.py 8080 1 40   # 异步
python3 experiments/slow-query-async/mixed_test.py 9090 1 40   # 同步
```

## 9. 重建对照版

`syncbuild/` 是本机保留的对照源码副本（`.gitignore` 不入库）。若需从主仓库重建：

```bash
cd <repo>
mkdir -p experiments/slow-query-async/syncbuild
cp -r src include CMakeLists.txt memory log experiments/slow-query-async/syncbuild/
cd experiments/slow-query-async/syncbuild
patch -p1 --dry-run < ../submit.patch   # 先试跑
patch -p1 < ../submit.patch              # 将 ThreadPool::submit 改为内联执行（从 syncbuild 根，路径 src/ThreadPool.cc）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target main -j4
# 产物：./bin/main（rpath 指向本目录 lib/，自包含）
```

## 10. 限制

- Debug 构建，绝对值不作生产参考，只看相对差异。
- 阻塞由 `SLEEP(3)` 精确模拟，非真实慢查询形态；真实慢查询时长分布不同。
- 旁观者与锁请求的连接是否落在同一 subLoop 受轮询分配影响，多跑几次更稳。
