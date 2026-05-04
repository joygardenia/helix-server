#include <Acceptor.h>
#include <InetAddress.h>
#include <Logger.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int CreateNonblocking() {
  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        IPPROTO_TCP);
  if (sockfd < 0) {
    LOG_ERROR << "Listen sockfd create err" << errno;
  }
  return sockfd;
}
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr,
                   bool reuseport)
    : loop_(loop), acceptSocket_(CreateNonblocking()),
      acceptChannel_(loop, acceptSocket_.fd()), listenning_(false) {
  acceptSocket_.setReuseAddr(true);
  acceptSocket_.setReusePort(reuseport);
  acceptSocket_.bindAddress(listenAddr);
  acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}
Acceptor::~Acceptor() {
  acceptChannel_.disableAll();
  acceptChannel_.remove();
}
void Acceptor::listen() {
  listenning_ = true;
  acceptSocket_.listen();
  acceptChannel_.enableReading();
}
void Acceptor::handleRead() {
  InetAddress peerAddr;
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0) { // TODO:理论上可以是0
    if (NewConnectionCallback_) {
      NewConnectionCallback_(connfd, peerAddr);
    } else {
      // LOG_ERROR<<""
      ::close(connfd);
    }
  } else {
    LOG_ERROR << "accept Err";
    if (errno == EMFILE) {
      LOG_ERROR << "sockfd reached limit";
    }
  }
}
