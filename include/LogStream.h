#pragma once
#include "FixedBuffer.h"
#include "noncopyable.h"
#include <string.h>
#include <string>

class GeneralTemplate : noncopyable {
public:
  GeneralTemplate() : data_(nullptr), len_(0) {}
  explicit GeneralTemplate(const char *cur, int size)
      : data_(cur), len_(size) {};
  const char *data_;
  int len_;
};
class LogStream : noncopyable {
public:
  using Buffer = FixedBuffer<kSmallBufferSize>;
  void append(const char *buf, int len) { buffer_.append(buf, len); }
  const Buffer &buffer() const { return buffer_; }
  void reset_buffer() { buffer_.reset(); }
  LogStream &operator<<(bool expr);
  LogStream &operator<<(short);
  LogStream &operator<<(unsigned short);
  LogStream &operator<<(int);
  LogStream &operator<<(unsigned int);
  LogStream &operator<<(long);
  LogStream &operator<<(unsigned long);
  LogStream &operator<<(long long);
  LogStream &operator<<(unsigned long long);
  LogStream &operator<<(float);
  LogStream &operator<<(double);
  LogStream &operator<<(char);
  LogStream &operator<<(const char *);
  LogStream &operator<<(const unsigned char *);
  LogStream &operator<<(const std::string &);
  LogStream &operator<<(const GeneralTemplate &);

private:
  static constexpr int kMaxNumberSize = 32;
  template <typename T> void formatInteger(T num);
  Buffer buffer_;
};
