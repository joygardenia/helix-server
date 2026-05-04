#include "Thread.h"
#include <EventLoop.h>
#include <EventLoopThread.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(nullptr), exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), name), mutex_(),
      cond_(), callback_(cb) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  if (loop_ != nullptr) {
    loop_->quit();
    thread_.join(); // TODO 让当前线程**阻塞等待**子线程执行完毕
  }
}

EventLoop *EventLoopThread::startLoop() {
  thread_.start();
  EventLoop *loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(mutex_); //! looping_
    cond_.wait(lock, [this]() { return loop_ != nullptr; });
    loop =
        loop_; // TODO
               // 用局部变量是防御性编程：在锁内把值复制出来，锁外只用局部变量，彻底避免竞态（其他地方修改）
    // TODO  return loop_;  // ✅ 在锁内直接返回也可以
  }
  return loop;
}

// 下面这个方法 是在单独的新线程里运行的
void EventLoopThread::threadFunc() {
  EventLoop loop;
  if (callback_) {
    callback_(&loop);
  }
  {
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }
  loop.loop();
  {
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
  }
}
