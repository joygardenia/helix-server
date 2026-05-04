# epoll → io_uring 替换记录

## 背景

框架是 Reactor 模式，核心循环：
```
epoll_wait 等事件 → 分发给 Channel → 业务回调
```
替换目标：把 `epoll_wait` 换成 io_uring，Channel 和业务回调不动。

新增文件：`include/IoUringPoller.h`、`src/IoUringPoller.cc`
切换方式：环境变量 `USE_IOURING=1 ./bin/main`

---

## v1：POLL_ADD one-shot 模式

用 `IORING_OP_POLL_ADD` 提交"监听 fd 事件"的请求，内核检测到事件后返回 CQE，从 CQE 取出 channel 指针放进 activeChannels。

**问题**：`POLL_ADD` 默认 one-shot，触发一次就失效，下次监听必须重新提交 SQE。每次事件触发都多一次 `submitPollAdd` + `io_uring_submit`，比 epoll 多了系统调用。

**结果**：QPS 比 epoll 低 35%。

---

## v2：IORING_POLL_ADD_MULTI 模式

**改动**：提交 SQE 时加 `sqe->len |= IORING_POLL_ADD_MULTI`，同时删掉 poll() 里每次重新提交的逻辑。

**原理**：MULTI 标志让一次提交持续监听，语义与 epoll level-triggered 一致，消除额外系统调用开销。需要 Linux 5.13+，当前内核 6.12 支持。

---

## 压测结果对比

| 并发 | epoll QPS | io_uring v1 QPS | io_uring v2 QPS | epoll P99 | io_uring v2 P99 |
|------|-----------|-----------------|-----------------|-----------|-----------------|
| 100  | 45,308    | 30,885          | 38,136          | 5.06ms    | 2.99ms          |
| 500  | 57,747    | 33,454          | 36,570          | 19.17ms   | 7.69ms          |
| 1000 | 51,204    | 33,026          | 38,355          | 37.51ms   | 16.93ms         |

**结论**：
- QPS：io_uring v2 比 epoll 低约 15%，原因是 POLL_ADD 模式仍是事件通知而非直接异步 IO
- P99 延迟：io_uring v2 全面优于 epoll，1000 并发下从 37ms 降至 17ms（降幅 55%）
- epoll 高并发 P99 抖动大的原因：`epoll_wait` 一次返回大量事件，尾部请求等待时间长；io_uring CQE 流式返回，处理更均匀

---

## 优化思路总结

1. 建立基准（epoll 压测）
2. 替换，跑数据，发现问题（v1 QPS 更低）
3. 定位原因（one-shot 导致额外系统调用，不是 io_uring 本身慢）
4. 查文档找解法（IORING_POLL_ADD_MULTI）
5. 验证（v2 数据）

**核心方法论：测量 → 分析 → 假设 → 验证，不靠直觉猜。**

---

## 进一步优化方向

当前实现仍是"事件通知"模式（io_uring 通知 fd 就绪，read/write 还是同步调用）。
真正的 io_uring 异步 IO 是用 `IORING_OP_RECV` / `IORING_OP_SEND` 直接提交读写操作，内核完成后返回数据，彻底消除 read/write 系统调用。这需要改造 TcpConnection 的 handleRead/sendInLoop，改动较大。
