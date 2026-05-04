#pragma once

#include <unordered_map>
#include <vector>

#include "Timestamp.h"
#include "noncopyable.h"

class EventLoop;
class Channel;
class Poller {
public:
  using ChannelList = std::vector<Channel *>;
  Poller(EventLoop *loop);
  virtual ~Poller() = default;
  virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
  virtual void updateChannel(Channel *channel) = 0;
  virtual void removeChannel(Channel *channel) = 0;
  static Poller *newDefaultPoller(EventLoop *loop);
  bool hasChannel(Channel *) const;

protected:
  using ChannelMap = std::unordered_map<int, Channel *>;
  ChannelMap channels_;

private:
  EventLoop *ownerLoop_;
};
