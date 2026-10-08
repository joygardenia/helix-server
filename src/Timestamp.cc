#include <Timestamp.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <sys/select.h>

Timestamp Timestamp::now() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int64_t seconds = tv.tv_sec;
  return Timestamp(seconds * Timestamp::kMicroSecondsPerSecond + tv.tv_usec);
}
std::string Timestamp::toString() const {
  time_t seconds =
      static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
  struct tm tmBuf;
  localtime_r(&seconds, &tmBuf);
  char buf[128] = {0};
  snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d", tmBuf.tm_year + 1900,
           tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour, tmBuf.tm_min,
           tmBuf.tm_sec);
  return buf;
}
std::string Timestamp::toFormattedString(bool showMicroseconds) const {
  char buf[64] = {0};
  time_t seconds =
      static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
  struct tm tmBuf;
  localtime_r(&seconds, &tmBuf);
  if (showMicroseconds) {
    int microseconds =
        static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
    snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d.%06d",
             tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
             tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, microseconds);
  } else {
    snprintf(buf, sizeof(buf), "%4d/%02d/%02d %02d:%02d:%02d",
             tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
             tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
  }
  return buf;
}
