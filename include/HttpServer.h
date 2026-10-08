#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Buffer.h"
#include "Callbacks.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include "http/HttpContext.h"
#include "http/HttpRequest.h"
#include "http/Router.h"
#include "noncopyable.h"

class EventLoop;
class ThreadPool;

class HttpServer : noncopyable {
public:
  HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name,
             ThreadPool *businessPool);
  Router &router() { return router_; }
  void setThreadNum(int n) { server_.setThreadNum(n); }
  void setIdleSeconds(int s) { server_.setIdleSeconds(s); }
  void setConnectionCallback(const ConnectionCallback &cb) {
    userConnectionCallback_ = cb;
  }
  void start() { server_.start(); }
  std::string dumpStats() const;

private:
  struct ConnState {
    ConnState() : processing(false) {}
    HttpContext ctx;
    std::deque<HttpRequest> pending;
    bool processing;
  };

  void onConnection(const TcpConnectionPtr &conn);
  void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp t);
  void pump(const TcpConnectionPtr &conn);
  std::shared_ptr<ConnState> getState(const TcpConnectionPtr &conn);
  void recordLatency(uint64_t us);

  TcpServer server_;
  ThreadPool *pool_;
  Router router_;
  ConnectionCallback userConnectionCallback_;
  mutable std::mutex mutex_;
  std::unordered_map<TcpConnection *, std::shared_ptr<ConnState>> states_;
  std::atomic<uint64_t> hist_[256];
  std::atomic<uint64_t> histCount_;
  std::atomic<uint64_t> histTotalUs_;
};
