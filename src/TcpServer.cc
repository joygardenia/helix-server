#include "Acceptor.h"
#include "Callbacks.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include <cstdio>
#include <functional>
#include <string.h>

#include <Logger.h>
#include <TcpConnection.h>
#include <TcpServer.h>
#include <memoryPool.h>
#include <sys/socket.h>
#include <unistd.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_FATAL << "main Loop is NULL!";
  }
  return loop;
}
//
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr,
                     const std::string &nameArg, Option option)
    : loop_(loop), ipPort_(listenAddr.toIpPort()), name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, nameArg)), started_(0),
      nextConnId_(1), idleSeconds_(0) {
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}
TcpServer::~TcpServer() {
  for (auto &item : connections_) {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
  }
}
void TcpServer::setThreadNum(int numThreads) {
  numThreads_ = numThreads;
  threadPool_->setThreadNum(numThreads_);
}
void TcpServer::start() {
  // 原子操作
  if (started_.fetch_add(1) == 0) {
    threadPool_->start(threadInitCallback_);
    loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
  }
}
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  // 轮询算法 选择一个subLoop 来管理connfd对应的channel
  EventLoop *ioLoop = threadPool_->getNextLoop();
  char buf[64] = {0};
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_; // 这里没有设置为原子类是因为其只在mainloop中执行
                 // 不涉及线程安全问题
  std::string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_.c_str()
           << "]- new connection [" << connName.c_str() << "]from "
           << peerAddr.toIpPort().c_str();

  // 通过sockfd获取其绑定的本机的ip地址和端口信息
  sockaddr_in local;
  ::memset(&local, 0, sizeof(local));
  socklen_t addrlen = sizeof(local);
  if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0) {
    LOG_ERROR << "sockets::getLocalAddr";
  }

  InetAddress localAddr(local);
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  // memoryPool::newElement<TcpConnection>(ioLoop, connName, sockfd, localAddr,
  // peerAddr),
  //[](TcpConnection* p){ memoryPool::deleteElement(p); });
  connections_[connName] = conn;
  // 下面的回调都是用户设置给TcpServer =>
  // TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite...
  // 这下面的回调用于handlexxx函数中
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setIdleSeconds(idleSeconds_);

  // 设置了如何关闭连接的回调
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));

  /*  EventLoop *ioLoop = threadPool_->getNextLoop();
    char buf[64];
    snprintf(buf, size_t maxlen, const char *__restrict format, ...)
    InetAddress localAddr;
    socklen_t len;
    if (::getsockname(sockfd, (sockaddr *)&localAddr, &len) < 0) {
      LOG_ERROR << "getsockname failed!";
    }
    TcpConnectionPtr conn(
        new TcpConnection(ioLoop, buf, sockfd, localAddr, peerAddr));
    connections_[buf] = conn;
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));*/
}
void TcpServer::removeConnection(const TcpConnectionPtr &connPtr) {
  loop_->runInLoop(
      std::bind(&TcpServer::removeConnectionInLoop, this, connPtr));
}
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &connPtr) {
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection %s" << connPtr->name();
  connections_.erase(connPtr->name());
  EventLoop *nloop = connPtr->getLoop();
  nloop->runInLoop(std::bind(&TcpConnection::connectDestroyed, connPtr));
}
