#include "Callbacks.h"
#include <cstddef>
#include <errno.h>
#include <fcntl.h> // for open
#include <functional>
#include <netinet/tcp.h>
#include <string.h>
#include <string>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h> // for close

#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>
#include <Socket.h>
#include <TcpConnection.h>

EventLoop *CheckLoopNotNull(EventLoop *loop) {
  if (loop == nullptr) {
    LOG_ERROR << "mainloop is nullptr";
  }
  return loop;
}
TcpConnection::TcpConnection(EventLoop *loop, const std::string &nameArg,
                             int sockfd, const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)), name_(nameArg), state_(kConnecting),
      reading_(true), socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)), localAddr_(localAddr),
      peerAddr_(peerAddr), highWaterMark_(64 * 1024 * 1024),
      idleSeconds_(0), lastRecvTime_() {
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
  LOG_INFO << "TcpConnection::ctor[" << name_ << "] at fd = " << sockfd;
  socket_->setKeepAlive(true);
}
TcpConnection::~TcpConnection() {
  LOG_INFO << "TcpConnection::dtor[" << name_ << "] at fd=" << channel_->fd()
           << " state=" << int(state_);
}

void TcpConnection::connectEstablished() {
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();
  connectionCallback_(shared_from_this());
  if (idleSeconds_ > 0) {
    resetIdleTimer();
  }
}
void TcpConnection::connectDestroyed() {
  if (state_ == kConnected) {
    setState(kDisconnected);
    channel_->disableAll();
    connectionCallback_(shared_from_this());
  }
  channel_->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime) {
  int saveErrno = 0;
  // 第一步：将数据从内核缓冲区拷贝到inputBuffer_用户态
  ssize_t readn = inputBuffer_.readFd(channel_->fd(), &saveErrno);
  if (readn > 0) {
    // 第二步：数据到手了，才调业务回调
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    if (idleSeconds_ > 0) {
      resetIdleTimer();
    }
  } else if (readn == 0) {
    handleClose();
  } else {
    errno = saveErrno;
    LOG_ERROR << "TcpConnection::handleRead failed!";
    handleError();
  }
}
void TcpConnection::handleWrite() {
  if (channel_->isWriting()) {
    int savedErrno = 0;
    size_t writen = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
    if (writen > 0) {
      if (idleSeconds_ > 0) {
        resetIdleTimer();
      }
      if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (writeCompleteCallback_) {
          loop_->queueInLoop(
              std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting) {
          shutdownInLoop();
        }
      }

    } else {
      LOG_ERROR << "TcpConnection::handleWrite";
    }
  } else {
    LOG_ERROR << "TcpConnection fd=" << channel_->fd()
              << "is down, no more writing";
  }
}
void TcpConnection::handleClose() {
  LOG_INFO << "TcpConnection::handleClose fd=" << channel_->fd()
           << " state=" << (int)state_;
  setState(kDisconnected);
  channel_->disableAll();
  TcpConnectionPtr connPtr(shared_from_this());
  connectionCallback_(connPtr);
  closeCallback_(connPtr);
}
void TcpConnection::handleError() {
  int optval;
  socklen_t optlen = sizeof optval;
  int err;
  if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) <
      0) {
    err = errno;
  } else {
    err = optval;
  }
  LOG_ERROR << "TcpConnection::handleError name:" << name_
            << " SO_ERROR:" << err;
}

void TcpConnection::resetIdleTimer() {
  // ioLoop 线程执行：刷新活跃时间，投递 idleSeconds_ 后到期的一次性定时器
  lastRecvTime_ = Timestamp::now();
  std::weak_ptr<TcpConnection> weak(shared_from_this());
  loop_->runAfter(idleSeconds_, [weak]() {
    if (auto conn = weak.lock()) {
      conn->onIdleTimeout();
    }
  });
}

void TcpConnection::onIdleTimeout() {
  // 期间有新活跃时旧定时器残留到期，lastRecvTime_ 已刷新，条件不成立
  Timestamp now = Timestamp::now();
  if (now.microSecondsSinceEpoch() - lastRecvTime_.microSecondsSinceEpoch() >=
      static_cast<int64_t>(idleSeconds_) * Timestamp::kMicroSecondsPerSecond) {
    LOG_INFO << "TcpConnection::onIdleTimeout fd=" << channel_->fd()
             << " idle " << idleSeconds_ << "s, force close";
    handleClose();
  }
}

void TcpConnection::send(const std::string &buf) {
  if (connected()) {
    if (loop_->isInLoopThread()) {
      sendInLoop(buf.c_str(), buf.size());
    } else {
      loop_->runInLoop(
          std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
    }
  } else {
    LOG_ERROR << "TcpConnection::send - not connected";
  }
}
void TcpConnection::sendInLoop(const void *data, size_t count) {
  if (state_ == kDisconnected) {
    LOG_ERROR << "disconnected, give up writing!";
    return;
  }

  ssize_t nwrote = 0;
  size_t remaining = count;
  bool faultError = false;

  // 如果当前 channel 没有在写，且发送缓冲区为空，尝试直接 write
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_->fd(), data, count);
    if (nwrote >= 0) {
      remaining -= nwrote;
      if (remaining == 0 && writeCompleteCallback_) {
        loop_->queueInLoop(
            std::bind(writeCompleteCallback_, shared_from_this()));
      }
    } else {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection::sendInLoop write failed!";
        if (errno == EPIPE || errno == ECONNRESET) {
          faultError = true;
        }
      }
    }
  }

  // 如果一次没写完，或者发生了致命错误，将剩余数据放入缓冲区（或放弃）
  if (!faultError && remaining > 0) {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining > highWaterMark_ && oldLen < highWaterMark_ &&
        highWaterMarkCallback_) {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(),
                                   oldLen + remaining));
    }
    outputBuffer_.append((char *)data + nwrote, remaining);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }
}
void TcpConnection::shutdown() {
  if (state_ == kConnected) {
    setState(kDisconnecting);
    loop_->queueInLoop(
        std::bind(&TcpConnection::shutdownInLoop, shared_from_this()));
  }
}
void TcpConnection::shutdownInLoop() {
  if (!channel_->isWriting()) {
    socket_->shutdownWrite();
  }
}
// 文件fileDescriptor->socket
// fileDescriptor 就是用 open("index.html", O_RDONLY) 打开的文件
// fd，内核直接把文件数据传到 socket，不经过用户态
void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count) {
  if (connected()) {
    if (loop_->isInLoopThread()) {
      sendFileInLoop(fileDescriptor, offset, count);
    } else {
      loop_->queueInLoop(std::bind(&TcpConnection::sendFileInLoop,
                                   shared_from_this(), fileDescriptor, offset,
                                   count));
    }
  } else {
    LOG_ERROR << "TcpConnection::sendFile - not connected";
  }
}
void TcpConnection::sendFileInLoop(int FileDescriptor, off_t offset,
                                   size_t count) {

  ssize_t nwrote = 0;
  size_t remaining = count;
  bool faultError = false;
  if (state_ == kDisconnected) {
    LOG_ERROR << "disconnected , give up writing!";
  }
  if (!channel_->isWriting()) {
    nwrote = ::sendfile(channel_->fd(), FileDescriptor, &offset, count);
    if (nwrote >= 0) {
      remaining -= nwrote;
      if (remaining == 0 && writeCompleteCallback_) {
        loop_->queueInLoop(
            std::bind(writeCompleteCallback_, shared_from_this()));
      }
    } else {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection::sendInLoop write failed!";
        if (errno == EPIPE || errno == ECONNRESET) {
          faultError = true;
        }
      }
    }
  }
  if (!faultError && remaining > 0) {
    loop_->queueInLoop(std::bind(&TcpConnection::sendFileInLoop,
                                 shared_from_this(), FileDescriptor, offset,
                                 remaining));
  }
}
