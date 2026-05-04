#include <sys/epoll.h>

#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), tied_(false), index_(-1) {}
Channel::~Channel() {}
void Channel::remove() { loop_->removeChannel(this); }
void Channel::update() { loop_->updateChannel(this); }
// tie() 只在 TcpConnection 建立时调用，专门解决连接被用户提前关闭的问题。
void Channel::tie(const std::shared_ptr<void> &obj) {
  tie_ = obj;
  tied_ = true;
}
void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {

    std::shared_ptr<void> guard =
        tie_.lock(); // tie_.lock() 把 weak_ptr 提升成 shared_ptr，只要 guard
                     // 活着，TcpConnection 就不会被销毁 用 void 是因为 Channel
                     // 不想依赖 TcpConnection 的具体类型，保持解耦
                     //  guard 活着 → TcpConnection 引用计数+1 →
                     //  回调执行期间不会被销毁 回调结束 → guard 析构 →
                     //  引用计数-1
    if (guard) {
      handleEventWithGuard(receiveTime);
    }

  } else {
    // tied_ 为 false 说明这个 Channel 没有绑定 TcpConnection,
    // 生命周期由其他机制管理，不需要通过 weak_ptr 检查，可以直接执行回调
    handleEventWithGuard(receiveTime);
  }
}
void Channel::handleEventWithGuard(Timestamp receiveTime) {
  // EPOLLHUP 表示连接挂断（对端关闭），EPOLLIN 表示还有数据可读
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    if (closeCallback_) { // TODO  ❌ 回调为空时直接调用会崩溃
      closeCallback_();
    }
  }
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }
  if (revents_ & EPOLLOUT) {
    if (writeCallback_) {
      writeCallback_();
    }
  }
  if (revents_ & EPOLLERR) {
    if (errorCallback_) {
      errorCallback_();
    }
  }
  if ((revents_ & (EPOLLPRI | EPOLLIN))) { //! const int Channel::kReadEvent =
                                           //! EPOLLIN | EPOLLPRI;整体与
    if (readCallback_) {
      readCallback_(receiveTime);
    }
  }
}
