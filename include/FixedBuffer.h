#pragma once
#include "noncopyable.h"
#include <string.h>
#include <string>
class AsynbLogging;
const int kSmallBufferSize = 4000;
const int kLargeBufferSize = 4000 * 1000;
template <int buffer_size> class FixedBuffer : noncopyable {
public:
  FixedBuffer() : size_(0), cur_(data_) {}
  void append(const char *value, size_t len) {
    if (len <= avail()) {
      strncpy(cur_, value, len);
      add(len);
    }
  }
  const char *data() const { return data_; }
  int length() const { return size_; }
  char *current() { return cur_; }
  size_t avail() const {
    return static_cast<size_t>(buffer_size - size_);
  } // 明确告诉编译器"我知道这里不会是负数，我主动转换"
    // 消除警告(int->size_t ,
    // 可能存在负数编译器提示报错)，同时让返回类型语义正确
  void add(size_t len) {
    cur_ += len;
    size_ += len;
  }
  void reset() {
    cur_ = data_;
    size_ = 0;
  }
  void bzero() {
    ::bzero(data_, sizeof(data_));
    reset();
  }
  std::string to_string() const { return std::string(data, size_); }

private:
  char data_[buffer_size];
  char *cur_;
  int size_;
};
