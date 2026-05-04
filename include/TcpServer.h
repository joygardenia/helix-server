#pragma once

/**
 * 用户使用muduo编写服务器程序
 **/

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "Acceptor.h"
#include "Buffer.h"
#include "Callbacks.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include "TcpConnection.h"
#include "Thread.h"
#include "noncopyable.h"

// 对外的服务器编程使用的类
class TcpServer {
public:
  using ThreadInitCallback = std::function<void(EventLoop *)>;
  enum Option { kNoReusePort, kReusePort };
  TcpServer(EventLoop *loop, const InetAddress &listenAddr,
            const std::string &nameArg, Option option = kNoReusePort);
  ~TcpServer();
  void setThreadInitCallback(const ThreadInitCallback &cb) {
    threadInitCallback_ = cb;
  }
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  }
  void setThreadNum(int numThreads);
  void start();

private:
  void newConnection(int sockfd, const InetAddress &peerAddr);
  void removeConnection(const TcpConnectionPtr &connPtr);
  void removeConnectionInLoop(const TcpConnectionPtr &connPtr);
  using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;
  EventLoop *loop_;
  const std::string ipPort_;
  const std::string name_;
  std::unique_ptr<Acceptor> acceptor_;
  std::shared_ptr<EventLoopThreadPool> threadPool_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;
  int numThreads_;
  std::atomic_int started_;
  int nextConnId_;
  ConnectionMap connections_;
};
