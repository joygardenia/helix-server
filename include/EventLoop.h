#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "CurrentThread.h"
#include "TimerQueue.h"
#include "Timestamp.h"
#include "noncopyable.h"
class Channel;
class Poller;

// 事件循环类 主要包含了两个大模块 Channel Poller(epoll的抽象)
class EventLoop : public noncopyable {
public:
  using Functor = std::function<void()>;
  EventLoop();
  ~EventLoop();
  void loop();
  void quit();
  Timestamp pollReturnTime() const { return pollReturnTime_; }
  void wakeup();
  void updateChannel(Channel *channel);
  void removeChannel(Channel *channel);
  bool hasChannel(Channel *channel);
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  void runAt(Timestamp timestamp, Functor &&cb) {
    timerQueue_->addTimer(std::move(cb), timestamp, 0.0);
  }
  void runAfter(double waitTime, Functor &&cb) {
    Timestamp timestamp(addTime(Timestamp::now(), waitTime));
    runAt(timestamp, std::move(cb));
  }
  void runEvery(double interval, Functor &&cb) {
    Timestamp timestamp(addTime(Timestamp::now(), interval));
    timerQueue_->addTimer(std::move(cb), timestamp, interval);
  }
  void runInLoop(Functor cb);
  void queueInLoop(Functor cb);

private:
  void handleRead();
  void doPendingFunctors();
  using ChannelList = std::vector<Channel *>;
  std::atomic_bool looping_;
  std::atomic_bool quit_;
  const pid_t threadId_;
  Timestamp pollReturnTime_;
  std::unique_ptr<Poller> poller_;
  std::unique_ptr<TimerQueue> timerQueue_;
  int wakeupFd_;
  std::unique_ptr<Channel> wakeupChannel_;
  ChannelList activeChannels_;
  std::atomic_bool callingPendingFunctors_;
  std::vector<Functor> pendingFunctors_;
  std::mutex mutex_;
};
