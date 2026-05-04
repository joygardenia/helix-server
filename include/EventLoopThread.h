#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

#include "Thread.h"
#include "noncopyable.h"

class EventLoop;
class EventLoopThread : noncopyable {
public:
  using ThreadInitCallback = std::function<void(EventLoop *)>;
  EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                  const std::string &name = std::string());
  ~EventLoopThread();
  EventLoop *startLoop();

private:
  void threadFunc();
  EventLoop *loop_;
  bool exiting_; // true :
                 // 表示对象处于销毁状态，防止析构期间其他操作误用这个对象。
  Thread thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  ThreadInitCallback callback_;
};
