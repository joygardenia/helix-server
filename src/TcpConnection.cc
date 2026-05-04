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
      peerAddr_(peerAddr), highWaterMark_(64 * 1024 * 1024) {
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
  // LOG_INFO << "sendInLoop start";
  ssize_t nwrote = 0;
  size_t remaining = count;
  bool faultError = false;
  if (state_ == kDisconnected) {
    LOG_ERROR << "disconnected , give up writing!";
  }
  //! outputBuffer_ 是**发送缓冲区**，只在内核 TCP
  //! 缓冲区满、一次写不完时才用到，正常情况下数据直接发出去，不经过它。
  if (!channel_->isWriting() &&
      outputBuffer_.readableBytes() ==
          0) { // Buffer里面没有数据可以直接写,如果有上次残留的数据则只能写入buffer,否则数据顺序会乱
    // LOG_INFO << "sendInLoop first";
    // LOG_INFO << "sendInLoop len=" << count;
    nwrote = ::write(channel_->fd(), data, count);
    LOG_INFO << "write returned nwrote=" << nwrote << " errno=" << errno;
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
  // 1.write没写完  2.outputBuffer_数据残留，这次数据只能append进buffer
  if (!faultError && remaining > 0) {
    // LOG_INFO << "sendInLoop second";
    size_t oldLen = outputBuffer_.readableBytes();
    // TODO
    // "oldLen<highWaterMark_"只在缓冲区从低水位越过高水位的那一刻触发一次，不重复触发.否则每次send
    // 都会触发回调，用户被反复通知"快满了"
    if (oldLen + remaining > highWaterMark_ && oldLen < highWaterMark_ &&
        highWaterMarkCallback_) {
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(),
                                   oldLen + remaining));
    }
    outputBuffer_.append((char *)data + nwrote, remaining);
    // epoll 检测到 fd 可写 → handleWrite()
    LOG_INFO << "before enableWriting: isWriting=" << channel_->isWriting();
    if (!channel_->isWriting()) {
      channel_->enableWriting();
      LOG_INFO << "enableWriting!";
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
