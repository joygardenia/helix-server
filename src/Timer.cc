#include "Timestamp.h"
#include <Timer.h>

void Timer::restart(Timestamp now) {
  if (repeat_) {
    expiration_ = addTime(now, interval_);
  } else {
    expiration_ =
        Timestamp(); // TODO : 内部为0
                     // 一次性定时器触发后不需要再次执行，把 expiration_
                     // 设为无效时间戳，TimerQueue
                     // 检查到无效时间就知道这个定时器不需要重新插入队列，直接销毁。
  }
}
