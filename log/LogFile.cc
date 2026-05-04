#include "LogFile.h"
#include "FileUtil.h"
#include <cstddef>
#include <ctime>
#include <mutex>
LogFile::LogFile(const std::string &basename, off_t rollsize, int flushInterval,
                 int checkEveryN)
    : basename_(basename), rollsize_(rollsize), flushInterval_(flushInterval),
      checkEveryN_(checkEveryN), startOfPeriod_(0), lastRoll_(0), lastFlush_(0),
      count_(0) {
  rollFile();
}
LogFile::~LogFile() = default;
void LogFile::append(const char *data, int len) {
  std::lock_guard<std::mutex> lg(mutex_); // TODO 特别  锁
  appendInlock(data, len);
}
void LogFile::flush() { file_->flush(); }
bool LogFile::rollFile() {
  time_t now = 0;
  std::string logfilename = getLogFileName(basename_, &now);
  time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;
  if (now > lastRoll_) { //! 不是 >startOfPeriod_
    lastRoll_ = now;
    lastFlush_ = now;
    // TODO : reset() → 旧 FileUtil 析构 → fclose() →
    // TODO : 自动 flush → 数据落盘 → 关闭旧文件 → 打开新文件
    // TODO : 不会有内容遗失
    startOfPeriod_ = start;
    file_.reset(new FileUtil(logfilename));
    return true;
  }
  return false;
}
std::string LogFile::getLogFileName(const std::string &basename, time_t *now) {
  //*now = time(NULL);
  // struct tm tm;
  std::string filename;
  filename.reserve(basename.size() + 64);
  filename = basename;

  char timebuf[32];
  struct tm tm;
  *now = time(NULL); // 获取当前时间
  localtime_r(now, &tm);
  strftime(timebuf, sizeof(timebuf), ".%Y%m%d-%H%M%S", &tm);

  filename += timebuf;
  filename += ".log";
  return filename;
}
void LogFile::appendInlock(const char *data, int len) {

  file_->append(data, len);
  time_t now = time(NULL);
  // roll
  count_++;
  if (file_->writtenBytes() > rollsize_) {
    rollFile();
  } else if (count_ >= checkEveryN_) {
    time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;
    if (start != startOfPeriod_) {
      rollFile();
    }
    count_ = 0;
  }
  // flush
  if (now - lastFlush_ > flushInterval_) {
    flush();
    lastFlush_ = now;
  }
};
