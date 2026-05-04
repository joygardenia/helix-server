#include "AsyncLogging.h"
#include "Buffer.h"
#include "LogFile.h"
#include <chrono>
#include <iterator>
#include <mutex>
#include <stdio.h>
#include <utility>
// 把慢操作（磁盘IO）从业务线程剥离出去，业务线程只做内存拷贝，性能损耗极小
AsyncLogging::AsyncLogging(const std::string &basename, off_t rollSize,
                           int flushInterval)
    : basename_(basename), rollSize_(rollSize), flushInterval_(flushInterval),
      mutex_(), cond_(), running_(false),
      thread_(std::bind(&AsyncLogging::ThreadFunc, this),
              "Logging"), // type : func()->void func->ptr
      currentBuffer_(new LargeBuffer), nextBuffer_(new LargeBuffer),
      buffers_() {
  currentBuffer_->bzero();
  nextBuffer_->bzero();
  buffers_.reserve(16);
}

void AsyncLogging::append(const char *logline, int len) {

  std::lock_guard<std::mutex> lg(mutex_);
  if (currentBuffer_->avail() > static_cast<size_t>(len)) {
    currentBuffer_->append(logline, len);
  } else {
    // buffers_.emplace_back(std::move(currentBuffer_));
    buffers_.push_back(std::move(
        currentBuffer_)); //! 还没写完但是要跟写完的一起塞进后端了
                          // TODO  之后 currentBuffer_ == nullptr，所有权已转移
                          // TODO move把currentBuffer_转为左值引用,触发
                          // unique_ptr 的**移动构造**，把所有权转移给 buffers_
                          // 里的新元素。
    if (nextBuffer_)
      currentBuffer_ = std::move(nextBuffer_); //! nextBuffer给currentBuffer了
    else
      currentBuffer_.reset(new LargeBuffer);
    cond_.notify_one();
  }
}
void AsyncLogging::ThreadFunc() {
  LogFile output(basename_, rollSize_);
  BufferPtr newBuffer1(new LargeBuffer);
  BufferPtr newBuffer2(new LargeBuffer);
  newBuffer1->bzero(); // new 出来的对象，内存里是**未初始化的垃圾值**，不是 0
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_) {
    {
      // TODO 交换缓冲区需要加锁。后端很少加锁，经常写日志file  but
      // logfile内部有锁
      std::unique_lock<std::mutex> lg(mutex_);
      if (buffers_.empty()) { // FIX 条件buffer_
        cond_.wait_for(lg, std::chrono::seconds(flushInterval_));
        // FIX wait_for 内部需要临时释放锁（等待期间解锁，唤醒后重新加锁）
        // FIX unique_lock 支持手动解锁，lock_guard 不能手动解锁，只能退出作用域
      }
      buffers_.push_back(std::move(currentBuffer_));
      currentBuffer_ = std::move(newBuffer1);
      if (!nextBuffer_)
        nextBuffer_ = std::move(
            newBuffer2); // TODO currentBuffer_ = std::move(nextBuffer_);
      buffers_.swap(buffersToWrite);
    }
    for (auto &buffer : buffersToWrite) {
      output.append(buffer->data(), buffer->length());
    }
    if (buffersToWrite.size() > 2) {
      buffersToWrite.resize(2);
    }
    if (!newBuffer1) { // TODO : 判断是否被用掉
      newBuffer1 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer1->reset();
    }

    if (!newBuffer2) {
      newBuffer2 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer2->reset();
    }
    // 不会内存泄漏，clear() 时每个元素的析构函数自动调用，unique_ptr 析构就
    // delete 掉堆上的 Buffer 对象
    buffersToWrite.clear();
    output.flush();
  }
  output.flush();
}
//  前端线程 -> currentBuffer_ 写满 → 移入 buffers_ → 当场用 currentBuffer_补上
//  后台线程 ->currentBuffer_(写完与否都)移入buffers_ -> newBuffer1 补上
//  newBuffer1 用完 → 从 buffersToWrite 里捞一个 reset 后复用
