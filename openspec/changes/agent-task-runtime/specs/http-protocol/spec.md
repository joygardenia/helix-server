## ADDED Requirements

### Requirement: Incremental HTTP parsing
系统 MUST 将 TCP 输入作为字节流增量解析，并在请求未完整到达时保留解析状态和未消费数据。

#### Scenario: Split request header
- **WHEN** 一个合法请求头被拆成多次 TCP 读取才接收完整
- **THEN** 系统不得调用业务 Handler，完整数据到达后只生成一个请求

#### Scenario: Multiple requests in one read
- **WHEN** 一次读取包含两个顺序 HTTP 请求
- **THEN** 系统 MUST 依次生成两个请求且不得丢弃第二个请求

### Requirement: Content length and request limits
系统 MUST 根据 `Content-Length` 判断请求体边界，并拒绝超过配置上限或格式不支持的请求。

#### Scenario: Incomplete request body
- **WHEN** 请求头声明的 body 长度大于当前已接收 body 长度
- **THEN** 系统 MUST 等待剩余数据且不得调用业务 Handler

#### Scenario: Oversized request
- **WHEN** 请求头或请求体超过配置的最大长度
- **THEN** 系统 MUST 返回 413 并关闭或明确终止该请求

### Requirement: HTTP connection lifecycle
系统 MUST 对合法请求返回明确的状态行、长度和连接语义，并对协议错误返回 4xx 响应。

#### Scenario: Valid keep-alive request
- **WHEN** 客户端发送合法的顺序 HTTP/1.1 请求并保持连接
- **THEN** 系统 MUST 返回完整响应并允许同一连接继续处理后续请求

#### Scenario: Malformed request line
- **WHEN** 客户端发送无法解析的方法、目标或版本
- **THEN** 系统 MUST 返回 400 且不得进入业务 Handler
