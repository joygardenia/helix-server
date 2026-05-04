#pragma once
#include "FileUtil.h"
#include <ctime>
#include <memory>
#include <mutex>

class LogFile {
public:
  LogFile(const std::string &basename, off_t rollsize, int flushInterval = 3,
          int checkEveryN = 1024);
  ~LogFile();
  void append(const char *data, int len);
  void flush();
  bool rollFile();

private:
  const std::string basename_;
  const off_t rollsize_;
  const int flushInterval_;
  const int checkEveryN_;
  int count_;
  std::mutex mutex_;
  time_t startOfPeriod_;
  time_t lastRoll_;
  time_t lastFlush_;
  std::unique_ptr<FileUtil> file_;
  const static int kRollPerSeconds_ = 60 * 60 * 24; // 直接赋值考虑 static
  //  public 函数调用的内部函数应该归于 private
  std::string getLogFileName(const std::string &basename, time_t *now);
  void appendInlock(const char *data, int len);
};
