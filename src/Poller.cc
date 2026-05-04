#include <Channel.h>
#include <Poller.h>

Poller::Poller(EventLoop *loop) : ownerLoop_(loop) {}
// 只判断 it != channels_.end() 只能说明这个 fd 存在，但不能保证存的就是这个
// channel 对象。 fd 可以复用，旧连接关闭后新连接可以拿到同一个f
bool Poller::hasChannel(Channel *channel) const {
  auto it = channels_.find(channel->fd());
  return it != channels_.end() && it->second == channel;
}
