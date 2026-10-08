#include "ThreadPool.h"

#include "Logger.h"

ThreadPool::ThreadPool(size_t threadCount, size_t maxQueueSize)
    : threadCount_(threadCount), maxQueueSize_(maxQueueSize), running_(false) {}

ThreadPool::~ThreadPool() { stop(); }

void ThreadPool::start() {
  if (running_.exchange(true))
    return;
  workers_.reserve(threadCount_);
  for (size_t i = 0; i < threadCount_; ++i)
    workers_.emplace_back(&ThreadPool::workerLoop, this);
}

void ThreadPool::stop() {
  if (!running_.exchange(false))
    return;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    notEmpty_.notify_all();
    notFull_.notify_all();
  }
  for (auto &w : workers_) {
    if (w.joinable())
      w.join();
  }
  workers_.clear();
}

bool ThreadPool::submit(Task task) {
  if (!running_.load()) {
    LOG_ERROR << "ThreadPool::submit rejected: pool not running";
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= maxQueueSize_) {
      LOG_ERROR << "ThreadPool::submit rejected: queue full ("
                << queue_.size() << "/" << maxQueueSize_ << ")";
      return false;
    }
    queue_.push(std::move(task));
  }
  notEmpty_.notify_one();
  return true;
}

size_t ThreadPool::pendingTasks() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return queue_.size();
}

void ThreadPool::workerLoop() {
  while (true) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      notEmpty_.wait(lock,
                     [this]() { return !running_.load() || !queue_.empty(); });
      if (!running_.load() && queue_.empty())
        return;
      task = std::move(queue_.front());
      queue_.pop();
    }
    notFull_.notify_one();
    task();
  }
}
