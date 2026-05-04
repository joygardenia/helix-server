#include "LogStream.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
static const char digits[] = "9876543210123456789";
template <typename T> void LogStream::formatInteger(T num) {
  // char buffer[1024];
  char *start = buffer_.current();
  char *cur = start;
  static const char *front = digits + 9;
  int part, mul;
  bool positive = num >= 0 ? true : false;
  do {
    mul = static_cast<int>(num) % 10;
    *cur++ = *(front + mul);
    num /= 10;
  } while (num);
  if (!positive)
    *cur++ = '-';
  *cur = '\0';
  std::reverse(start, cur);
  buffer_.add(cur - start);
}
LogStream &LogStream::operator<<(bool expr) {
  buffer_.append(expr ? "true" : "false", expr ? 4 : 5);
  return *this;
}
LogStream &LogStream::operator<<(short number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(unsigned short number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(int number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(unsigned int number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(long number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(unsigned long number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(long long number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(unsigned long long number) {
  formatInteger(number);
  return *this;
}
LogStream &LogStream::operator<<(float number) {
  return *this << static_cast<double>(number);
}
LogStream &LogStream::operator<<(double number) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.12g", number);
  buffer_.append(buffer, strlen(buffer));
  return *this;
}
LogStream &LogStream::operator<<(char str) {
  buffer_.append(&str, 1);
  return *this;
}
LogStream &LogStream::operator<<(const char *str) {
  buffer_.append(str, std::strlen(str));
  return *this;
}
LogStream &LogStream::operator<<(const unsigned char *str) {
  buffer_.append(reinterpret_cast<const char *>(str), sizeof(str));
  return *this;
}
LogStream &LogStream::operator<<(const std::string &str) {
  buffer_.append(str.c_str(), str.size()); // FIX not sizeof(str)
  return *this;
}
LogStream &LogStream::operator<<(const GeneralTemplate &g) {
  buffer_.append(g.data_, g.len_);
  return *this;
}
