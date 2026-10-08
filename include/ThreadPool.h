#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
  using Task = std::function<void()>;

  explicit ThreadPool(size_t threadCount, size_t maxQueueSize);
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  void start();
  void stop();

  bool submit(Task task);

  size_t pendingTasks() const;

private:
  void workerLoop();

  size_t threadCount_;
  size_t maxQueueSize_;
  mutable std::mutex mutex_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  std::queue<Task> queue_;
  std::vector<std::thread> workers_;
  std::atomic_bool running_;
};
