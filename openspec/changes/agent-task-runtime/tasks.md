## 1. Build Boundary and Core Transport

- [ ] 1.1 Separate source-only build inputs from generated `build/`, `bin/`, `lib/` and log artifacts, and make the default executable build without MariaDB or Etcd.
- [ ] 1.2 Fix `Buffer::writeFd` to consume exactly the bytes written and add Buffer tests for append, retrieve, partial write and reuse.
- [ ] 1.3 Fix cross-thread `TcpConnection::send` ownership so queued callbacks retain payload data until execution, and add a regression test for deferred sending.
- [ ] 1.4 Remove synchronous hot-path logging from Buffer append and verify the network send path still emits required error logs.

## 2. HTTP Protocol and Routing

- [ ] 2.1 Add standalone `HttpRequest`, `HttpResponse` and incremental `HttpParser` modules with explicit incomplete, complete, oversized and malformed outcomes.
- [ ] 2.2 Add parser tests for split headers, split body, multiple requests in one read, missing/invalid `Content-Length`, malformed request lines and request limits.
- [ ] 2.3 Integrate the parser with `TcpConnection`/message handling so incomplete bytes remain buffered and one callback processes one completed request.
- [ ] 2.4 Add Router and Handler boundaries, move response generation out of `main.cc`, and add route matching/error tests.
- [ ] 2.5 Define and test HTTP/1.1 connection behavior for sequential keep-alive requests and protocol error responses.

## 3. Agent Task Runtime

- [ ] 3.1 Add typed task state, task ID, cancellation token, task result and task handler interfaces without external AI dependencies.
- [ ] 3.2 Implement a fixed-capacity task queue and worker runtime with explicit queue-full rejection and clean worker ownership.
- [ ] 3.3 Implement task submission, status query and cancellation handlers for `POST /v1/tasks`, `GET /v1/tasks/{id}` and `DELETE /v1/tasks/{id}`.
- [ ] 3.4 Add local handlers for `health_check`, `system_info` and `memory_pool_benchmark`, keeping execution bounded and deterministic.
- [ ] 3.5 Add timeout handling, terminal-state idempotence, result expiry/reclamation and tests for success, failure, pending cancellation, queue exhaustion and timeout.
- [ ] 3.6 Ensure worker completion updates state and sends responses through the owning EventLoop rather than touching connection state from worker threads.

## 4. Observability and Overload Protection

- [ ] 4.1 Add bounded request/task counters and fixed latency buckets for `/metrics` output.
- [ ] 4.2 Add a bounded slow-request ring buffer and `/slowlog` query behavior with deterministic eviction tests.
- [ ] 4.3 Add request body, active task, queue capacity and execution-time limits with explicit 4xx/429/503 responses.
- [ ] 4.4 Add a single-process token-bucket limiter and tests for refill, burst allowance and rejection.
- [ ] 4.5 Add concurrency and load scripts that verify metrics, queue rejection, slow records and no unbounded growth.

## 5. Lifecycle and Default Service

- [ ] 5.1 Remove game routes, game database initialization, hard-coded credentials, ConsistentHash demo code and Etcd registration from the default executable.
- [ ] 5.2 Add signal-to-EventLoop shutdown notification and stop accepting new connections before resource teardown.
- [ ] 5.3 Add graceful drain for existing connections and tasks with a configured shutdown timeout and forced cleanup after expiry.
- [ ] 5.4 Add tests for signal handling, new-request rejection during shutdown, task/connection drain, timeout exit and repeated shutdown signals.
- [ ] 5.5 Reduce `main.cc` to dependency assembly and startup, and verify the new service can start and serve local task endpoints without external services.

## 6. Verification and Resume Evidence

- [ ] 6.1 Add CTest registration for unit, integration and loopback HTTP tests without requiring external databases or Etcd.
- [ ] 6.2 Run focused tests, full build, sanitizer/concurrency checks where available, and loopback manual verification for every endpoint.
- [ ] 6.3 Run reproducible baseline/final benchmarks for request throughput, task latency, queue rejection and shutdown behavior; record environment and parameters.
- [ ] 6.4 Review the final source diff to exclude generated artifacts and document remaining limitations before using the project in a resume.
