#include "FileUtil.h"
#include <cstdio>
#include <cstring>
FileUtil::FileUtil(std::string &file_name)
    : file_(fopen(file_name.c_str(), "ae")), writtenBytes_(0) {
  ::setbuffer(file_, buffer_, sizeof(buffer_));
}
FileUtil::~FileUtil() {
  if (file_)
    ::fclose(file_);
}; // TODO : if(file_)
void FileUtil::append(const char *data, size_t len) {

  size_t written = 0;
  while (written != len) {
    size_t remain = len - written;
    size_t n = write(data + written, remain);
    if (n != remain) {
      int err = ferror(file_);
      if (err) {
        fprintf(stderr, "AppendFile::append() failed %s\n", strerror(err));
        clearerr(file_); //  清除文件指针的错误标志
        break;
      }
    }
    written += n;
  }
  writtenBytes_ += len; // TODO : written->len
}
void FileUtil::flush() { ::fflush(file_); }
// off_t FileUtil::writtenBytes() const { return writtenBytes_; }
// 对于这种一行的简单函数，通常直接内联在头文件里更简洁。
// 编译器会隐式加上 inline，这就是为什么可以把简单函数写在 .h 里而不报链接错误
size_t FileUtil::write(const char *data, size_t len) {
  return fwrite_unlocked(data, 1, len, file_);
}
