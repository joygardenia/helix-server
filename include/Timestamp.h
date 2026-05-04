#ifndef TIME_STAMP_H
#define TIME_STAMP_H

#include <cstdint>
#include <iostream>
#include <string>
#include <sys/time.h>
class Timestamp {
public:
  Timestamp() : microSecondsSinceEpoch_(0) {}
  explicit Timestamp(int64_t microSecondsSinceEpoch)
      : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}
  static Timestamp now();
  std::string toString() const;

  std::string toFormattedString(bool showMicroseconds = false) const;
  int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
  int64_t SecondsSinceEpoch() const {
    return microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
  }
  static Timestamp invalid() { return Timestamp(); }
  static const int kMicroSecondsPerSecond = 1000 * 1000;

private:
  int64_t microSecondsSinceEpoch_;
};

inline bool operator<(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs) {
  return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

// 如果是重复定时任务就会对此时间戳进行增加。
inline Timestamp addTime(Timestamp timestamp, double seconds) {
  // 将延时的秒数转换为微妙
  int64_t delta =
      static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
  // 返回新增时后的时间戳
  return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}

#endif
