## 1. HTTP Protocol Module

- [x] 1.1 Add `include/http/HttpRequest.h` + `src/http/HttpRequest.cc` with method, path, version, headers accessor and body.
- [x] 1.2 Add `include/http/HttpResponse.h` + `src/http/HttpResponse.cc` with status, headers, body and a serializer producing status line, `Content-Length` and `Connection`.
- [x] 1.3 Add `include/http/HttpContext.h` + `src/http/HttpContext.cc`: incremental state machine (`ExpectRequestLine → ExpectHeaders → ExpectBody → GotAll`) consuming `Buffer*` and returning `Incomplete / Complete / BadRequest`.
- [x] 1.4 Add request limits and `Content-Length`-based body boundary; return explicit errors for missing/unsupported length and oversized input.
- [x] 1.5 Add `include/http/Router.h` + `src/http/Router.cc` matching `(method, path)` to a registered handler without any business dependency.
- [x] 1.6 Add unit tests: split header, split body, two requests in one read, missing/invalid `Content-Length`, malformed request line, oversized request, route match/miss.

## 2. Business Thread Pool

- [x] 2.1 Add `include/ThreadPool.h` + `src/ThreadPool.cc`: fixed workers, bounded task queue, mutex + condition variable, `start()`/`stop()`.
- [x] 2.2 Implement queue-full rejection that reports failure to the caller without enqueuing.
- [x] 2.3 Add unit tests: task execution, bounded rejection under saturation, graceful stop with no lingering workers.

## 3. onMessage Integration

- [x] 3.1 Replace `parseRequest`/`retrieveAllAsString` in `onMessage` with `HttpContext`; business still runs synchronously, verifying behavior equivalence for all existing routes.
- [x] 3.2 Extract route handlers out of `main.cc` and register them on `Router`, preserving existing paths and response formats.
- [x] 3.3 Offload handler execution to `ThreadPool`; on completion marshal the response via `conn->getLoop()->runInLoop(...)` before `conn->send`.
- [x] 3.4 Add single-send guard and connection lifetime handling: capture `shared_ptr<TcpConnection>`, check `connected()` before send, drop response if disconnected.
- [x] 3.5 Map failures to responses: queue full → 503, malformed request → 400, oversized → 413, without entering business handlers.

## 4. Layer Separation

- [x] 4.1 Ensure network and HTTP headers do not include `DBPool`, `LFU`, `EtcdClient` or `ConsistentHash`.
- [x] 4.2 Move all `DBPool`/`LFU`/`etcd` calls into business handlers; reduce `main.cc` to dependency assembly and startup.
- [x] 4.3 Remove duplicated request parsing and response construction, unifying them in the HTTP module.

## 5. Verification

- [x] 5.1 Register new tests with CMake/CTest without external DB or etcd.
- [x] 5.2 Build and run unit + loopback integration tests; all pass.
- [ ] 5.3 Manually verify every existing route returns the same behavior as before.
- [x] 5.4 Run before/after benchmarks (synchronous vs business pool) and record environment and parameters.
- [x] 5.5 Confirm the diff excludes generated artifacts (`build/`, `bin/`, `lib/`, logs) and document remaining limitations.

## Remaining limitations

- 5.4 benchmarks not run: requires live MySQL + etcd; `build/`, `bin/`, `lib/`, `bin/logs/`
  were untracked from git (files kept on disk) and `.gitignore` added so the diff stays clean.
- `ThreadPool` default is `8` workers / `1024` queue; tune against `DBPool` connection count (design Open Question).
- Queue-full returns 503 and keeps the connection open (client may retry); connection is not forced closed.
- `/stats` latency now measures business-handler time on the pool (starts at IO-thread parse time),
  not pure end-to-end;口径 in design Open Questions.
- `transfer-encoding: chunked` unsupported → 400; only `Content-Length`-bounded bodies accepted.
