
#pragma once
#include "Channel.h"
#include "Timestamp.h"

#include <functional>
#include <set>
#include <utility>
#include <vector>

class EventLoop;
class Timer;

class TimerQueue {
public:
  using TimerCallback = std::function<void()>;
  TimerQueue(EventLoop *loop);
  ~TimerQueue();
  void addTimer(TimerCallback cb, Timestamp when, double interval);

private:
  using Entry =
      std::pair<Timestamp,
                Timer *>; // TODO   Timestamp会比较大小，直接存值会方便比较
  using TimerList = std::set<Entry>;
  void addTimerInLoop(Timer *timer);
  void handleRead();
  void resetTimerfd(int timerfd, Timestamp expiration);
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry> &expired, Timestamp now);
  bool insert(Timer *timer);
  EventLoop *loop_;
  const int timerfd_;
  Channel timerfdChannel_;
  TimerList timers_;
  bool callingExpiredTimers_;
};
