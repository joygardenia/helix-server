#include "HttpServer.h"

#include <functional>
#include <memory>
#include <sstream>

#include "EventLoop.h"
#include "ThreadPool.h"

HttpServer::HttpServer(EventLoop *loop, const InetAddress &addr,
                       const std::string &name, ThreadPool *businessPool)
    : server_(loop, addr, name), pool_(businessPool), histCount_(0),
      histTotalUs_(0) {
  for (size_t i = 0; i < 256; ++i)
    hist_[i].store(0, std::memory_order_relaxed);
  server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this,
                                          std::placeholders::_1));
  server_.setMessageCallback(
      std::bind(&HttpServer::onMessage, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
}

std::shared_ptr<HttpServer::ConnState>
HttpServer::getState(const TcpConnectionPtr &conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(conn.get());
  return it == states_.end() ? std::shared_ptr<ConnState>() : it->second;
}

void HttpServer::onConnection(const TcpConnectionPtr &conn) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn->connected()) {
      states_[conn.get()] = std::make_shared<ConnState>();
    } else {
      states_.erase(conn.get());
    }
  }
  if (userConnectionCallback_)
    userConnectionCallback_(conn);
}

void HttpServer::onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp) {
  std::shared_ptr<ConnState> state = getState(conn);
  if (!state)
    return;

  for (;;) {
    HttpContext::ParseResult result = state->ctx.parseRequest(buf);
    if (result == HttpContext::kComplete) {
      state->pending.push_back(state->ctx.request());
      state->ctx.reset();
      continue;
    }
    if (result == HttpContext::kIncomplete)
      break;

    HttpResponse resp(true);
    resp.setStatusCode(
        static_cast<HttpResponse::HttpStatusCode>(state->ctx.errorCode()));
    resp.setBody("{\"error\":\"bad request\"}");
    state->ctx.reset();
    state->pending.clear();
    if (conn->connected())
      conn->send(resp.toString());
    return;
  }

  pump(conn);
}

void HttpServer::pump(const TcpConnectionPtr &conn) {
  std::shared_ptr<ConnState> state = getState(conn);
  if (!state || state->processing || state->pending.empty())
    return;

  HttpRequest req = state->pending.front();
  state->pending.pop_front();
  state->processing = true;
  const int64_t start = Timestamp::now().microSecondsSinceEpoch();

  bool accepted = pool_->submit([this, conn, req, start]() {
    HttpResponse resp(false);
    if (!router_.route(req, &resp)) {
      resp.setStatusCode(HttpResponse::k404NotFound);
      resp.setBody("{\"error\":\"not found\"}");
    }
    std::shared_ptr<std::string> out =
        std::make_shared<std::string>(resp.toString());
    const bool close = resp.closeConnection();

    conn->getLoop()->runInLoop([this, conn, out, close, start]() {
      if (conn->connected())
        conn->send(*out);
      uint64_t used = static_cast<uint64_t>(
          Timestamp::now().microSecondsSinceEpoch() - start);
      recordLatency(used);
      if (close && conn->connected())
        conn->shutdown();
      std::shared_ptr<ConnState> st = getState(conn);
      if (st)
        st->processing = false;
      pump(conn);
    });
  });

  if (!accepted) {
    state->processing = false;
    state->pending.clear();
    HttpResponse resp(true);
    resp.setStatusCode(HttpResponse::k503ServiceUnavailable);
    resp.setBody("{\"error\":\"server busy\"}");
    if (conn->connected())
      conn->send(resp.toString());
  }
}

void HttpServer::recordLatency(uint64_t us) {
  histCount_.fetch_add(1, std::memory_order_relaxed);
  histTotalUs_.fetch_add(us, std::memory_order_relaxed);
  int bucket = (us == 0) ? 0 : (63 - __builtin_clzll(us));
  if (bucket >= 256)
    bucket = 255;
  hist_[bucket].fetch_add(1, std::memory_order_relaxed);
}

std::string HttpServer::dumpStats() const {
  uint64_t total = histCount_.load(std::memory_order_relaxed);
  if (total == 0)
    return "{\"count\":0}";

  uint64_t target[6] = {total / 2,       total * 3 / 4,      total * 9 / 10,
                        total * 99 / 100, total * 999 / 1000, total};
  const char *names[6] = {"p50_us", "p75_us", "p90_us",
                          "p99_us", "p999_us", "max_us"};
  uint64_t cum = 0;
  int idx = 0;
  bool first = true;
  std::ostringstream os;
  os << "{\"count\":" << total << ",\"avg_us\":"
     << (histTotalUs_.load(std::memory_order_relaxed) / total) << ",";
  for (int b = 0; b < 256 && idx < 6; ++b) {
    cum += hist_[b].load(std::memory_order_relaxed);
    while (idx < 6 && cum >= target[idx]) {
      if (!first)
        os << ",";
      first = false;
      uint64_t lo = (b == 0) ? 0 : (1ULL << b);
      os << "\"" << names[idx] << "\":" << lo;
      ++idx;
    }
  }
  os << "}";
  return os.str();
}
