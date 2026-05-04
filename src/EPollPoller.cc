#include "Timestamp.h"
#include <cerrno>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <Channel.h>
#include <EPollPoller.h>
#include <Logger.h>
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(16) {
  if (epollfd_ < 0) {
    LOG_FATAL
        << "epoll_create error:%d \n"
        << strerror(
               errno); // TODO 转成可读字符串 errno
                       // 是系统调用失败时设置的**错误码**（一个整数），error
                       // 只是你日志里写的字符串。
  }
}
EPollPoller::~EPollPoller() { ::close(epollfd_); }
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels) {
  Timestamp now(Timestamp::now());
  int numEvent =
      ::epoll_wait(epollfd_, &*events_.begin(), events_.size(), timeoutMs);
  int SaveErrno = errno;
  if (numEvent > 0) {
    LOG_INFO << "events happen -> num : " << numEvent;
    fillActiveChannels(numEvent, activeChannels);
    if (numEvent == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  } else if (numEvent == 0) {
    LOG_INFO << "timeout!";
  } else {
    if (errno != EINTR) {
      errno = SaveErrno;
      LOG_ERROR << "EPollPoller::poll() error! : " << strerror(errno);
    }
  }
  return now;
}
void EPollPoller::updateChannel(Channel *channel) {
  const int index = channel->index();
  LOG_INFO << "func =>" << " fd " << channel->fd()
           << " events=" << channel->events() << " index= " << index;
  if (index == kNew || index == kDeleted) {
    if (index == kNew) {
      int fd = channel->fd();
      channels_[fd] = channel;
    }
    // kDeleted 不做多余处理
    channel->set_index(kAdded);
    update(EPOLL_CTL_ADD, channel);
  } else {
    if (channel->isNoneEvent()) {
      channel->set_index(kDeleted);
      update(EPOLL_CTL_DEL, channel);
    } else {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}
void EPollPoller::removeChannel(Channel *channel) {
  int fd = channel->fd();
  channels_.erase(fd);

  const int index = channel->index();
  if (index == kAdded) {
    update(EPOLL_CTL_DEL, channel);
  }
  channel->set_index(kNew); // FIX 不是KDeleted channels_ 彻底删除
}

void EPollPoller::fillActiveChannels(int numEvent,
                                     ChannelList *activeChannels) const {
  for (int i = 0; i < numEvent; i++) {
    // TODO : 指向对应的channel  void* ->Channel*
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
    channel->set_revents(events_[i].events);
    activeChannels->push_back(channel);
  }
}
void EPollPoller::update(int operation, Channel *channel) {
  epoll_event event;
  ::memset(&event, 0, sizeof(event));

  int fd = channel->fd();
  event.events = channel->events();
  event.data.fd = fd;
  event.data.ptr = channel;

  if (::epoll_ctl(epollfd_, operation, fd, &event) <
      0) { // TODO:epoll_ctl 调用时内核会把 event
           // 的内容**复制**到内核空间，函数返回后 event
           // 局部变量就可以销毁了，内核自己保存了一份。
    if (operation == EPOLL_CTL_DEL) {
      LOG_ERROR << "epoll_ctl del error:" << errno;
    } else {
      LOG_ERROR << "epoll_ctl mod/add error:" << errno;
    }
  }
  LOG_INFO << "update";
}
