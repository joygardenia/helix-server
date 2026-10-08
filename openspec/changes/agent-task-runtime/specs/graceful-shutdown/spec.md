## ADDED Requirements

### Requirement: Signal-driven graceful shutdown
系统 MUST 将 SIGINT 或 SIGTERM 转换为 EventLoop 可处理的停机事件，不得在异步信号处理函数中执行复杂资源操作。

#### Scenario: Stop accepting new connections
- **WHEN** 服务收到停机信号
- **THEN** Acceptor MUST 停止接收新连接，已有连接仍可完成当前请求

### Requirement: Drain tasks and connections
系统 MUST 在退出前拒绝新任务并尝试排空已有任务和连接。

#### Scenario: Drain completed work
- **WHEN** 所有已有任务和连接在停机超时内完成
- **THEN** 服务 MUST 释放 worker、定时器、socket 和日志资源后退出

#### Scenario: Shutdown timeout
- **WHEN** 任务或连接在停机超时内仍未完成
- **THEN** 服务 MUST 关闭剩余资源并退出，且不得无限等待
