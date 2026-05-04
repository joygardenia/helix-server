#include "CurrentThread.h"
#include "Timestamp.h"
#include <cstdint>
#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <sys/eventfd.h>
#include <unistd.h>

#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>
#include <Poller.h>
#include <vector>

thread_local EventLoop *t_loopInThisThread = nullptr;
const int kPollTimeMs = 10000;

int CreateEventfd() {
  int evtfd = ::eventfd(
      0, EFD_NONBLOCK | EFD_CLOEXEC); // TODO eventfd 只需要**1个fd**，pipe
                                      // 需要**2个fd**（读端+写端）
  if (evtfd < 0) {
    LOG_FATAL << "eventfd errno : " << errno;
  }
  return evtfd;
}
EventLoop::EventLoop()
    : looping_(false), quit_(false), threadId_(CurrentThread::tid()),
      wakeupFd_(CreateEventfd()), poller_(Poller::newDefaultPoller(this)),
      wakeupChannel_(new Channel(this, wakeupFd_)),
      callingPendingFunctors_(false) {
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread) {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  } else {
    t_loopInThisThread = this;
  }
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  wakeupChannel_->enableReading();
}
EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = nullptr;
}
// loop() 每次循环：
//   1. epoll_wait → IO事件回调（handleEvent）
//   2. 执行跨线程任务（doPendingFunctors）

// 一个响应外部（网络事件），一个响应内部（线程间任务）。
void EventLoop::loop() {
  looping_ = true;
  quit_ = false;
  while (!quit_) {
    activeChannels_.clear();
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    for (Channel *channel : activeChannels_) {
      channel->handleEvent(pollReturnTime_); // FIX 处理 IO 事件触发的回调
    }
    doPendingFunctors(); // FIX 处理**主线程投递**的回调
  }
  LOG_INFO << "EventLoop stop looping";
  looping_ = false;
}
void EventLoop::handleRead() { // TODO :读回调
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    LOG_ERROR << "EventLoop::handleRead reads " << n << "bytes instead of 8";
  }
}
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    LOG_ERROR << "EventLoop::handleRead writes" << n << "bytes instead of 8";
  }
}
void EventLoop::updateChannel(Channel *channel) {
  poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel *channel) {
  poller_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel *channel) {
  return poller_->hasChannel(channel);
}
void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
  }
}
// TODO mainloop => subloop
void EventLoop::runInLoop(Functor cb) { // 其他线程(也许是自身)投递的回调
  if (isInLoopThread())
    cb();
  else
    queueInLoop(cb);
}
void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(
        mutex_); // TODO : mutex_ => pendingFunctors_
    pendingFunctors_.push_back(cb);
  }
  if (!isInLoopThread() || callingPendingFunctors_) { // calling -> 正在
    wakeup();
  }
}
void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (const Functor &functor : functors) {
    functor();
  }
  callingPendingFunctors_ = false;
}
