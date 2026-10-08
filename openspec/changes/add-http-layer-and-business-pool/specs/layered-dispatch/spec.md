## ADDED Requirements

### Requirement: One-way dependency direction
网络层与 HTTP 层 MUST NOT 依赖 `DBPool`、`LFU`、`EtcdClient` 或 `ConsistentHash`；这些依赖 MUST 仅出现在业务层。

#### Scenario: Network and HTTP headers free of business deps
- **WHEN** 检查网络层与 HTTP 层的头文件包含关系
- **THEN** 其中 MUST NOT 出现数据库、缓存、etcd 或一致性哈希的头文件

### Requirement: Single response per request
每个请求 MUST 最多发送一次响应，重复完成不得产生重复发送。

#### Scenario: Duplicate completion ignored
- **WHEN** 同一请求的完成回调被触发多次
- **THEN** 系统 MUST 只发送一次响应

### Requirement: Connection lifetime across async processing
异步处理期间系统 MUST 保证连接对象存活，并在连接已断开时丢弃响应而不访问已释放对象。

#### Scenario: Connection closed before completion
- **WHEN** 连接在业务完成前断开，随后业务完成
- **THEN** 系统 MUST 丢弃该响应且不得访问已销毁的连接

### Requirement: Router matches without business coupling
Router MUST 仅依据 `(method, path)` 匹配 Handler，且 MUST NOT 包含业务依赖。

#### Scenario: Route match dispatch
- **WHEN** 一个请求路径匹配已注册路由
- **THEN** Router MUST 返回对应 Handler 且不访问数据库或缓存
