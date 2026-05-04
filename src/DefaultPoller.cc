#include <stdlib.h>

#include <EPollPoller.h>
#include <IoUringPoller.h>
#include <Poller.h>

Poller *Poller::newDefaultPoller(EventLoop *loop) {
  if (::getenv("USE_IOURING")) {
    return new IoUringPoller(loop);
  } else {
    return new EPollPoller(loop);
  }
}
