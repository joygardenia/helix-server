#pragma once

#include "Logger.h"
#include <algorithm>
#include <cstddef>
#include <stddef.h>
#include <string>
#include <vector>
class Buffer {
public:
  static const size_t kCheapPrepend = 8;
  static const size_t kInitialSize = 1024;
  Buffer(size_t size = kInitialSize)
      : buffer_(size + kCheapPrepend), readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {}
  size_t readableBytes() const { return writerIndex_ - readerIndex_; }
  size_t writableBytes() const { return buffer_.size() - writerIndex_; }
  size_t prependableBytes() const { return readerIndex_; }

  const char *peek() const { return begin() + readerIndex_; } // forget
  void retrieve(size_t len) {
    if (len < readableBytes())
      readerIndex_ += len;
    else
      retrieveAll();
  }
  void retrieveAll() {
    // readerIndex_ = writerIndex_;
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
  }
  std::string retrieveAsString(size_t len) {
    std::string result(peek(), len);
    retrieve(len);
    return result;
  }
  std::string retrieveAllAsString() {
    // size_t len = readableBytes();
    // std::string result(peek(), len);
    // retrieveAll();
    // return result;
    return retrieveAsString(
        readableBytes()); // FIX : not writeable
                          // writableBytes()
                          // 是缓冲区剩余可写空间，不是已读入的数据长度，所以读出了大量垃圾数据
  }
  void ensureWritableBytes(size_t len) {
    if (len > writableBytes())
      makeSpace(len);
  }
  void append(const char *data, size_t len) {
    LOG_INFO << "outputBuffer append";
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    writerIndex_ += len;
  }
  char *beginWrite() { return begin() + writerIndex_; }
  const char *beginWrite() const { return begin() + writerIndex_; }
  ssize_t readFd(int fd, int *saveErrno);
  ssize_t writeFd(int fd, int *saveErrno);
  char *begin() { return &buffer_.front(); }
  const char *begin() const { return &buffer_.front(); }

private:
  void makeSpace(size_t len) {
    if (writableBytes() + prependableBytes() <
        len + kCheapPrepend) // 也就是说 len > xxx前面剩余的空间 + writer的部分
    {
      buffer_.resize(writerIndex_ + len);
    } else // 这里说明 len <= xxx + writer 把reader搬到从xxx开始
    {
      size_t readable = readableBytes(); // readable = reader的长度
      std::copy(begin() + readerIndex_, begin() + writerIndex_,
                begin() + kCheapPrepend);
      readerIndex_ = kCheapPrepend;
      writerIndex_ = readerIndex_ + readable;
    }
  }
  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};
