#include "EventLoop.h"
#include "Thread.h"
#include <cstdio>
#include <memory>

#include <EventLoopThread.h>
#include <EventLoopThreadPool.h>
#include <Logger.h>
#include <vector>
EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop,
                                         const std::string &nameArg)
    : baseLoop_(baseLoop), name_(nameArg), started_(false), numThreads_(0),
      next_(0) {}
EventLoopThreadPool::~EventLoopThreadPool() {}
void EventLoopThreadPool::start(const ThreadInitCallback &cb) {
  char buf[name_.size() + 32];
  for (int i = 0; i < numThreads_; i++) {
    snprintf(buf, sizeof(buf), "%s%d", name_.c_str(), i);
    EventLoopThread *t;
    t = new EventLoopThread(cb, buf);
    threads_.push_back(std::unique_ptr<EventLoopThread>(t));
    loop_.push_back(t->startLoop());
  }
}
EventLoop *EventLoopThreadPool::getNextLoop() {
  EventLoop *loop = baseLoop_;
  if (!loop_.empty()) {
    loop = loop_[next_];
    next_++;
    if (next_ >= loop_.size())
      next_ = 0;
  }
  return loop;
}
std::vector<EventLoop *> EventLoopThreadPool::getAllLoops() {
  if (loop_.empty()) {
    return std::vector<EventLoop *>(1, baseLoop_);
  }
  return loop_;
}
