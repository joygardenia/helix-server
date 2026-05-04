#pragma once

#include <sys/epoll.h>
#include <vector>

#include "Channel.h"
#include "Poller.h"
#include "Timestamp.h"

/**
 * epoll的使用:
 * 1. epoll_create
 * 2. epoll_ctl (add, mod, del)
 * 3. epoll_wait
 **/

class EPollPoller : public Poller {
public:
  using EventList = std::vector<epoll_event>;
  EPollPoller(EventLoop *loop);
  ~EPollPoller() override;
  Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
  void updateChannel(Channel *channel) override;
  void removeChannel(Channel *channel) override;

private:
  static const int kInitEventListSize = 16;
  void fillActiveChannels(int numEvent, ChannelList *activeChannels) const;
  void update(int operation, Channel *channel);
  int epollfd_;
  EventList events_;
};
