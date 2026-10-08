#pragma once

#include <functional>
#include <memory>

class Buffer;
class TcpConnection;
class Timestamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>; //! shared_ptr
using TcpConnectionWeakPtr = std::weak_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;
// 高水位回调，流量控制
using HighWaterMarkCallback =
    std::function<void(const TcpConnectionPtr &, size_t)>;
using MessageCallback =
    std::function<void(const TcpConnectionPtr &, Buffer *, Timestamp)>;
