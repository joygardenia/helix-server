#include "Buffer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpConnection.h"
#include "TcpServer.h"
#include "ThreadPool.h"
#include "http/HttpContext.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/Router.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

class TestServer {
public:
  TestServer(EventLoop *loop, const InetAddress &addr, ThreadPool *pool,
             Router *router)
      : server_(loop, addr, "TestServer"), pool_(pool), router_(router) {
    server_.setConnectionCallback([](const TcpConnectionPtr &) {});
    server_.setMessageCallback(
        [this](const TcpConnectionPtr &conn, Buffer *buf, Timestamp) {
          onMessage(conn, buf);
        });
    server_.setThreadNum(2);
  }
  void start() { server_.start(); }

private:
  void onMessage(const TcpConnectionPtr &conn, Buffer *buf) {
    HttpContext *ctx = nullptr;
    {
      std::lock_guard<std::mutex> lock(m_);
      auto &slot = contexts_[conn.get()];
      if (!slot)
        slot.reset(new HttpContext(64 * 1024));
      ctx = slot.get();
    }
    while (buf->readableBytes() > 0) {
      HttpContext::ParseResult r = ctx->parseRequest(buf);
      if (r == HttpContext::kIncomplete)
        return;
      if (r == HttpContext::kBadRequest || r == HttpContext::kTooLarge) {
        HttpResponse resp(true);
        resp.setStatusCode(r == HttpContext::kTooLarge
                               ? HttpResponse::k413PayloadTooLarge
                               : HttpResponse::k400BadRequest);
        resp.setBody("{}");
        conn->send(resp.toString());
        {
          std::lock_guard<std::mutex> lock(m_);
          contexts_.erase(conn.get());
        }
        conn->shutdown();
        return;
      }
      HttpRequest req(ctx->request());
      ctx->reset();
      TcpConnectionWeakPtr weak(conn);
      bool queued = pool_->submit([this, req, weak]() {
        HttpResponse resp(false);
        if (!router_->route(req, &resp)) {
          resp.setStatusCode(HttpResponse::k404NotFound);
          resp.setBody("{}");
        }
        TcpConnectionPtr c = weak.lock();
        if (!c)
          return;
        c->getLoop()->runInLoop([c, resp]() {
          if (c->connected())
            c->send(resp.toString());
        });
      });
      if (!queued) {
        HttpResponse resp(true);
        resp.setStatusCode(HttpResponse::k503ServiceUnavailable);
        resp.setBody("{}");
        conn->send(resp.toString());
        return;
      }
    }
  }

  TcpServer server_;
  ThreadPool *pool_;
  Router *router_;
  std::mutex m_;
  std::map<const TcpConnection *, std::shared_ptr<HttpContext>> contexts_;
};

static int connectTo(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

static std::string recvUntil(int fd, const std::string &needle) {
  std::string acc;
  for (int i = 0; i < 400; ++i) {
    if (acc.find(needle) != std::string::npos)
      return acc;
    char tmp[4096];
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    acc.append(tmp, n);
  }
  return acc;
}

int main() {
  uint16_t port = 0;
  {
    int tmp = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ::bind(tmp, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(tmp, reinterpret_cast<sockaddr *>(&a), &len);
    port = ntohs(a.sin_port);
    ::close(tmp);
  }

  Router router;
  router.addRoute("GET", "/hello", [](const HttpRequest &, HttpResponse *resp) {
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("text/plain");
    resp->setBody("world");
  });

  ThreadPool pool(4, 64);
  pool.start();

  EventLoop loop;
  InetAddress addr(port, "127.0.0.1");
  TestServer server(&loop, addr, &pool, &router);
  server.start();

  std::thread loopThread([&loop]() { loop.loop(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // 正常请求 + 同连接顺序第二个请求
  {
    int fd = connectTo(port);
    CHECK(fd >= 0);
    std::string req = "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(fd, req.c_str(), req.size(), 0);
    std::string resp = recvUntil(fd, "world");
    CHECK(resp.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(resp.find("world") != std::string::npos);

    ::send(fd, req.c_str(), req.size(), 0);
    std::string resp2 = recvUntil(fd, "world");
    CHECK(resp2.find("HTTP/1.1 200 OK") != std::string::npos);
    ::close(fd);
  }

  // 未匹配路由 → 404
  {
    int fd = connectTo(port);
    CHECK(fd >= 0);
    std::string req = "GET /missing HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(fd, req.c_str(), req.size(), 0);
    std::string resp = recvUntil(fd, "404");
    CHECK(resp.find("404") != std::string::npos);
    ::close(fd);
  }

  // 非法请求行 → 400
  {
    int fd = connectTo(port);
    CHECK(fd >= 0);
    std::string req = "GARBAGE\r\n\r\n";
    ::send(fd, req.c_str(), req.size(), 0);
    std::string resp = recvUntil(fd, "400");
    CHECK(resp.find("400") != std::string::npos);
    ::close(fd);
  }

  loop.quit();
  loopThread.join();
  pool.stop();

  if (g_failures == 0)
    std::printf("loopback_test: ALL PASS\n");
  else
    std::printf("loopback_test: %d FAILURES\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
