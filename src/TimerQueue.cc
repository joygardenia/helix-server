#include "Timestamp.h"
#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>
#include <Timer.h>
#include <TimerQueue.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <functional>
#include <iterator>
#include <string.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>
int CreateTimerfd() {
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0) {
    LOG_ERROR << "Failed in timerfd_create!";
  }
  return timerfd;
}
void readTimerFd(int timerfd) {
  uint64_t read_bytes;
  ssize_t readn = read(timerfd, &read_bytes, sizeof(read_bytes));
  if (readn != sizeof(read_bytes)) {
    // LOG_ERROR<<"Failed in readTimerfd";//不像 eventfd 需要 write
    // 来触发，timerfd 由内核在到期时自动触发
    LOG_ERROR << "TimerQueue::readTimerFd read " << readn
              << " bytes instead of 8";
  }
}
TimerQueue::TimerQueue(EventLoop *loop)
    : loop_(loop), timerfd_(CreateTimerfd()), timerfdChannel_(loop_, timerfd_),
      timers_() {
  timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
  timerfdChannel_.enableReading();
}
TimerQueue::~TimerQueue() {
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  for (const Entry &timer : timers_) {
    delete timer.second; // timer.first 是值类型（int64_t），随 pair
                         // 自动销毁，不需要手动处理
  }
}
void TimerQueue::addTimer(TimerCallback cb, Timestamp when, double interval) {
  Timer *timer = new Timer(std::move(cb), when, interval);
  loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
}

void TimerQueue::addTimerInLoop(Timer *timer) {
  bool earliestChanged = insert(timer);
  if (earliestChanged) {
    resetTimerfd(timerfd_, timer->expiration());
  }
}
void TimerQueue::handleRead() {
  Timestamp now = Timestamp::now();
  readTimerFd(timerfd_);
  std::vector<Entry> expired = getExpired(now);
  callingExpiredTimers_ = true;
  for (Entry &entry : expired) {
    entry.second->run();
  }
  callingExpiredTimers_ = false;
  reset(expired, now);
}
void TimerQueue::resetTimerfd(int timerfd, Timestamp expiration) {
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memset(&newValue, 0, sizeof(newValue));
  memset(&oldValue, 0, sizeof(oldValue));
  int64_t microSecondDif = expiration.microSecondsSinceEpoch() -
                           Timestamp::now().microSecondsSinceEpoch();
  if (microSecondDif < 100)
    microSecondDif = 100;
  struct timespec ts;
  ts.tv_sec =
      static_cast<time_t>(microSecondDif / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microSecondDif % Timestamp::kMicroSecondsPerSecond) * 1000);
  newValue.it_value = ts;
  // 此函数会唤醒事件循环
  if (::timerfd_settime(timerfd_, 0, &newValue, &oldValue)) {
    LOG_ERROR << "timerfd_settime faield()";
  }
}
// ❌ 返回局部变量引用&，函数结束后 expired 销毁，引用悬空
// TODO : 值引用
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now) {
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer *>(UINTPTR_MAX));
  TimerList::iterator end = timers_.lower_bound(sentry);
  std::copy(timers_.begin(), end, std::back_inserter(expired));
  timers_.erase(timers_.begin(), end);
  return expired;
}
void TimerQueue::reset(const std::vector<Entry> &expired, Timestamp now) {
  bool needReset = false;
  for (const Entry &t : expired) {
    if (t.second->repeat()) {
      needReset = true;
      t.second->restart(now);
      insert(t.second);
    } else {
      delete t.second;
    }
  }
  if (needReset) { // FIX : 检测到有insert() => false!!!
    if (!timers_
             .empty()) // FIX:处理完了新的一批超时定时器，timerfd
                       // 的触发时间已经过期失效，必须无条件重置为下一个最早到期时间，不需要比较
      resetTimerfd(timerfd_, timers_.begin()->second->expiration());
  }
}
bool TimerQueue::insert(Timer *timer) {
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator begin = timers_.begin();
  if (begin == timers_.end() || when < begin->first) {
    earliestChanged = true;
  }
  timers_.insert(Entry(when, timer));
  return earliestChanged;
}
