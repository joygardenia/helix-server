#pragma once

#include "FixedBuffer.h"
#include "LogFile.h"
#include "LogStream.h"
#include "Thread.h"
#include "noncopyable.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
class AsyncLogging {
public:
  AsyncLogging(const std::string &basename, off_t rollSize,
               int flushInterval = 3);
  ~AsyncLogging() {
    if (running_)
      stop();
  };
  void append(const char *logline, int len);
  void start() {
    running_ = true;
    thread_.start();
  }
  void stop() {
    running_ = false;
    cond_.notify_one();
  }

private:
  using LargeBuffer = FixedBuffer<
      kLargeBufferSize>; //! FixedBuffer<kLargeBufferSize>LargeBuffer
  using BufferVector = std::vector<std::unique_ptr<LargeBuffer>>;
  using BufferPtr = BufferVector::value_type;
  void ThreadFunc();
  const int flushInterval_;
  std::atomic<bool> running_;
  const std::string basename_;
  const off_t rollSize_;
  Thread thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  BufferPtr currentBuffer_;
  BufferPtr nextBuffer_;
  BufferVector buffers_;
};
