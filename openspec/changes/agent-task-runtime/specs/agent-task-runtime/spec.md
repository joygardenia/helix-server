## ADDED Requirements

### Requirement: Bounded task submission
系统 MUST 提供任务提交接口，并使用有界队列限制等待任务数量。

#### Scenario: Submit accepted task
- **WHEN** 客户端提交已注册任务且队列仍有容量
- **THEN** 系统 MUST 返回唯一任务 ID并将任务置为 Pending 或 Running

#### Scenario: Queue exhausted
- **WHEN** 客户端提交任务时队列已满
- **THEN** 系统 MUST 拒绝任务并返回 429 或 503，且不得无限增长内存

### Requirement: Task lifecycle
系统 MUST 为任务提供 Pending、Running、Succeeded、Failed、Cancelled 和 TimedOut 等可观察终态或中间态。

#### Scenario: Successful task
- **WHEN** worker 完成一个合法本地任务
- **THEN** 任务 MUST 进入 Succeeded 并保存可查询结果

#### Scenario: Failed task
- **WHEN** 任务执行函数返回失败
- **THEN** 任务 MUST 进入 Failed 并保存有限错误信息

### Requirement: Task cancellation and timeout
系统 MUST 支持未开始任务取消和运行时间上限，并保证任务终态只被设置一次。

#### Scenario: Cancel pending task
- **WHEN** 客户端取消仍处于 Pending 的任务
- **THEN** 任务 MUST 进入 Cancelled 且不得再被 worker 执行

#### Scenario: Task timeout
- **WHEN** 任务超过配置的执行时间上限
- **THEN** 任务 MUST 进入 TimedOut 或按取消令牌停止，并释放任务占用的结果资源

### Requirement: Local task registry
系统 MUST 支持注册和查找无外部服务依赖的本地任务处理器。

#### Scenario: Unknown task type
- **WHEN** 客户端提交未注册的任务类型
- **THEN** 系统 MUST 返回 400 且不创建可运行任务

#### Scenario: Health task
- **WHEN** 客户端提交 `health_check`
- **THEN** 系统 MUST 返回可验证的本地健康状态结果
