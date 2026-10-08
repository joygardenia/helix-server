## ADDED Requirements

### Requirement: Blocking work executes off the IO thread
系统 MUST 将业务处理提交到业务线程池执行，IO 线程 MUST NOT 同步等待业务完成。

#### Scenario: DB-bound request does not block IO thread
- **WHEN** 一个请求的业务处理包含阻塞的数据库调用
- **THEN** 该 subLoop 上的其他连接 MUST 仍能继续处理事件

### Requirement: Bounded task queue with explicit rejection
业务线程池 MUST 使用有界任务队列；队列满时 MUST 明确拒绝请求并返回 503，MUST NOT 无界堆积。

#### Scenario: Queue full rejection
- **WHEN** 队列已满且新请求到达
- **THEN** 系统 MUST 返回 503 且不得将该请求排入队列

### Requirement: Marshaled response back to IO thread
业务完成后 MUST 通过连接所属 `EventLoop::runInLoop` 回到该连接所在 IO 线程执行发送。

#### Scenario: Response sent from owning loop
- **WHEN** 业务线程完成请求处理
- **THEN** 响应 MUST 经连接所属 EventLoop 投递，且实际发送发生在该 IO 线程

### Requirement: Worker lifecycle
线程池 MUST 支持启动与停止；停止后 MUST 不再接收新任务，并结束 worker 线程。

#### Scenario: Graceful stop
- **WHEN** 服务收到停止信号且线程池被关闭
- **THEN** 线程池 MUST 停止接收新任务并回收 worker，不遗留运行中的线程
