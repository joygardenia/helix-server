#include <InetAddress.h>
#include <Logger.h>
#include <Socket.h>
#include <asm-generic/socket.h>
#include <cstddef>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
Socket::~Socket() { ::close(sockfd_); }
void Socket::bindAddress(const InetAddress &localaddr) {
  if (0 != ::bind(sockfd_, (sockaddr *)localaddr.getSockAddr(),
                  sizeof(sockaddr_in))) {
    LOG_FATAL << "bind sockfd:" << sockfd_ << "fail";
  }
}
void Socket::listen() {
  if (::listen(
          sockfd_,
          1024)) { // TODO : backlog就是这个等待队列的最大长度。
                   // 服务器accept()来不及处理时，内核帮你先把连接排队，不会立刻丢弃。
    LOG_FATAL << "listen sockfd:" << sockfd_ << "fail";
  }
}
int Socket::accept(InetAddress *peeraddr) {
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  ::memset(&addr, 0, len);
  // accept 需要fcnlt额外调用
  // accept4 一步到位，而且是**原子操作**，避免了两次 fcntl
  // 之间的竞态条件（多线程 fork 时可能出问题）
  //- SOCK_NONBLOCK — 非阻塞，read/write 没数据时立刻返回而不是阻塞等待
  //- SOCK_CLOEXEC — exec 新进程时自动关闭这个 fd，防止 fd 泄漏给子进程
  int connfd =
      ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (connfd >= 0) {
    peeraddr->setSockAddr(addr);
  }
  return connfd;
}
void Socket::shutdownWrite() {
  if (::shutdown(sockfd_, SHUT_WR) < 0) {
    LOG_ERROR << "shutdown error";
  }
}
void Socket::setTcpNoDelay(bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}
// 开启 SO_REUSEADDR：
// 服务器崩溃重启：bind(8080) → 成功，忽略 TIME_WAIT
void Socket::setReuseAddr(bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}
void Socket::setReusePort(bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}
void Socket::setKeepAlive(bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
};
