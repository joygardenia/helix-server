#pragma once

#include <liburing.h>
#include <vector>

#include "Channel.h"
#include "Poller.h"
#include "Timestamp.h"

// io_uring 替代 epoll 的 Poller 实现
// 接口与 EPollPoller 完全相同，可通过 USE_IOURING 编译开关切换
class IoUringPoller : public Poller {
public:
    static const int kQueueDepth = 256;

    IoUringPoller(EventLoop *loop);
    ~IoUringPoller() override;

    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;

private:
    // 为 channel 对应的 fd 提交 POLL_ADD 请求（监听可读/可写事件）
    void submitPollAdd(Channel *channel);
    // 取消 channel 对应的 POLL_ADD 请求
    void submitPollRemove(Channel *channel);

    struct io_uring ring_;
};
