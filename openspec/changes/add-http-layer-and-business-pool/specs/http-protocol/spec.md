## ADDED Requirements

### Requirement: Incremental HTTP parsing
系统 MUST 以字节流增量方式解析 HTTP 请求，并在请求未完整到达时保留解析状态和未消费数据。

#### Scenario: Split request header
- **WHEN** 一个合法请求被拆成多次 TCP 读取才接收完整
- **THEN** 系统不得调用业务 Handler，完整到达后只生成一个请求

#### Scenario: Multiple requests in one read
- **WHEN** 一次读取包含两个顺序 HTTP 请求
- **THEN** 系统 MUST 依次生成两个请求且不得丢弃第二个请求

### Requirement: Request body boundary by Content-Length
系统 MUST 依据 `Content-Length` 判断请求体边界，并在请求体未收齐时等待剩余数据。

#### Scenario: Incomplete request body
- **WHEN** 请求头声明的 body 长度大于已接收 body 长度
- **THEN** 系统 MUST 等待剩余数据且不得调用业务 Handler

#### Scenario: Missing or unsupported length
- **WHEN** 请求声明需要 body 但缺少 `Content-Length` 或使用不支持的 `Transfer-Encoding`
- **THEN** 系统 MUST 返回 4xx 且不得进入业务 Handler

### Requirement: Request and response objects
系统 MUST 提供 `HttpRequest` 与 `HttpResponse`；`HttpResponse` MUST 能序列化为合法 HTTP/1.1 报文，包含状态行、`Content-Length` 和 `Connection`。

#### Scenario: Serialize response
- **WHEN** 业务 Handler 生成一个 `HttpResponse`
- **THEN** 序列化结果 MUST 为合法状态行 + 头部 + 与 `Content-Length` 一致的 body

### Requirement: Sequential keep-alive
系统 MUST 在同一连接上顺序处理多个请求，且不丢失后续请求。

#### Scenario: Two sequential keep-alive requests
- **WHEN** 客户端在同一连接顺序发送两个请求并保持连接
- **THEN** 系统 MUST 依次返回两个完整响应，且连接保持可用

### Requirement: Malformed and oversized handling
系统 MUST 对无法解析的请求行或头部返回 400，对超过配置上限的请求返回 413，且不得进入业务 Handler。

#### Scenario: Malformed request line
- **WHEN** 客户端发送无法解析的方法、目标或版本
- **THEN** 系统 MUST 返回 400 且不得进入业务 Handler

#### Scenario: Oversized request
- **WHEN** 请求头或请求体超过配置的最大长度
- **THEN** 系统 MUST 返回 413 并明确终止该请求
