#pragma once
#include <stdio.h>
#include <string>
#include <sys/types.h> //off_t

class FileUtil {
public:
  FileUtil(std::string &file_name);
  ~FileUtil();
  void append(const char *data, size_t len);
  void flush();
  off_t writtenBytes() const { return writtenBytes_; }

private:
  size_t write(const char *data, size_t len);
  FILE *file_;
  char buffer_[65536];
  off_t writtenBytes_;
};
