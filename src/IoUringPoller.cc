#include "IoUringPoller.h"

#include <cstring>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <stdexcept>

#include "Channel.h"
#include "Logger.h"
#include "Timestamp.h"

IoUringPoller::IoUringPoller(EventLoop *loop) : Poller(loop) {
    if (io_uring_queue_init(kQueueDepth, &ring_, 0) < 0) {
        throw std::runtime_error("io_uring_queue_init failed");
    }
}

IoUringPoller::~IoUringPoller() {
    io_uring_queue_exit(&ring_);
}

// 用 IORING_OP_POLL_ADD 监听 fd 事件，语义与 epoll_wait 等价
// 每次 CQE 返回后需要重新提交（one-shot 模式）
Timestamp IoUringPoller::poll(int timeoutMs, ChannelList *activeChannels) {
    // 提交所有待提交的 SQE
    io_uring_submit(&ring_);

    struct io_uring_cqe *cqe;
    struct __kernel_timespec ts;
    ts.tv_sec  = timeoutMs / 1000;
    ts.tv_nsec = (timeoutMs % 1000) * 1000000LL;

    // 等待至少一个事件完成
    int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
    Timestamp now(Timestamp::now());

    if (ret == -ETIME) {
        LOG_INFO << "IoUringPoller timeout";
        return now;
    }
    if (ret < 0) {
        LOG_ERROR << "io_uring_wait_cqe_timeout error: " << strerror(-ret);
        return now;
    }

    // 收割所有已完成的 CQE
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(&ring_, head, cqe) {
        ++count;
        Channel *channel = static_cast<Channel *>(io_uring_cqe_get_data(cqe));
        if (!channel) continue;

        // channel 可能已被 removeChannel 删除，必须校验
        if (channels_.find(channel->fd()) == channels_.end() ||
            channels_[channel->fd()] != channel) {
            continue;
        }

        int res = cqe->res;
        if (res < 0) {
            LOG_ERROR << "poll cqe error fd=" << channel->fd()
                      << " err=" << strerror(-res);
            channel->set_revents(EPOLLERR);
        } else {
            channel->set_revents(res);
        }
        activeChannels->push_back(channel);
        // MULTI 模式下内核持续监听，无需重新提交
    }
    io_uring_cq_advance(&ring_, count);

    return now;
}

void IoUringPoller::updateChannel(Channel *channel) {
    const int fd = channel->fd();
    if (channels_.find(fd) == channels_.end()) {
        channels_[fd] = channel;
    }
    if (channel->isNoneEvent()) {
        // 不监听任何事件，不提交 POLL_ADD
        return;
    }
    submitPollAdd(channel);
}

void IoUringPoller::removeChannel(Channel *channel) {
    channels_.erase(channel->fd());
    submitPollRemove(channel);
}

void IoUringPoller::submitPollAdd(Channel *channel) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        LOG_ERROR << "io_uring SQ full, fd=" << channel->fd();
        return;
    }
    unsigned poll_mask = 0;
    if (channel->events() & EPOLLIN)  poll_mask |= POLLIN;
    if (channel->events() & EPOLLOUT) poll_mask |= POLLOUT;

    io_uring_prep_poll_add(sqe, channel->fd(), poll_mask);
    // IORING_POLL_ADD_MULTI: 持续监听，不需要每次重新提交
    sqe->len |= IORING_POLL_ADD_MULTI;
    io_uring_sqe_set_data(sqe, channel);
}

void IoUringPoller::submitPollRemove(Channel *channel) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;
    // user_data 必须与提交时一致，用 channel 指针的整数值
    io_uring_prep_poll_remove(sqe, (uint64_t)(uintptr_t)channel);
    io_uring_sqe_set_data(sqe, nullptr);
}
