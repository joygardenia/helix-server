## ADDED Requirements

### Requirement: Request and task metrics
系统 MUST 提供请求、连接、任务状态、队列和拒绝数量的进程内统计接口。

#### Scenario: Metrics after request
- **WHEN** 客户端访问 `/metrics` 且服务已经处理请求
- **THEN** 返回结果 MUST 包含请求总数、错误数、活跃连接数和任务计数

### Requirement: Latency and slow request records
系统 MUST 使用有界统计结构记录请求延迟，并记录超过阈值的慢请求。

#### Scenario: Slow request
- **WHEN** 一个请求处理时间超过配置阈值
- **THEN** 系统 MUST 将其加入有界慢请求记录并可通过 `/slowlog` 查询

#### Scenario: Slow log capacity
- **WHEN** 慢请求记录达到容量上限后继续产生慢请求
- **THEN** 系统 MUST 按既定淘汰策略覆盖旧记录且不得无限增长内存

### Requirement: Overload protection
系统 MUST 对请求体、并发任务和任务队列设置资源上限，并在超过上限时返回明确错误。

#### Scenario: Concurrent task limit
- **WHEN** 运行中任务数已经达到配置上限
- **THEN** 新任务 MUST 进入有界等待队列或被明确拒绝

#### Scenario: Rate limit exceeded
- **WHEN** 单机令牌桶在一个请求到达时没有可用令牌
- **THEN** 系统 MUST 返回 429 且不得执行该请求的任务处理器
