// ping-pong 压测客户端：独立 epoll 非阻塞实现，N 连接严格 ping-pong
// (写满一个消息 -> 读回一个消息 -> 再写)，统计吞吐。
// 用法: ./bin/pingpong_client --port 9000 --conns 100 --size 16384 --duration 10
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>

namespace {

int SetNonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Conn {
  int fd = -1;
  bool connecting = true;  // 连接尚未完成
  bool wantWrite = true;   // 需要写下一个 ping
  size_t wpos = 0;         // 当前 ping 已写字节
  size_t rpos = 0;         // 当前 pong 已读字节
};

}  // namespace

int main(int argc, char *argv[]) {
  const char *ip = "127.0.0.1";
  int port = 9000;
  int conns = 100;
  size_t msgSize = 16384;
  int duration = 10;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--ip" && i + 1 < argc)
      ip = argv[++i];
    else if (a == "--port" && i + 1 < argc)
      port = std::atoi(argv[++i]);
    else if (a == "--conns" && i + 1 < argc)
      conns = std::atoi(argv[++i]);
    else if (a == "--size" && i + 1 < argc)
      msgSize = std::strtoul(argv[++i], nullptr, 10);
    else if (a == "--duration" && i + 1 < argc)
      duration = std::atoi(argv[++i]);
  }
  if (conns < 1)
    conns = 1;
  if (msgSize == 0)
    msgSize = 1;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    fprintf(stderr, "[client] invalid ip: %s\n", ip);
    return 1;
  }

  int epfd = ::epoll_create1(0);
  if (epfd < 0) {
    perror("epoll_create1");
    return 1;
  }

  std::vector<Conn> cs(conns);
  for (int i = 0; i < conns; ++i) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      perror("socket");
      continue;
    }
    SetNonBlocking(fd);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int r = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) {
      ::close(fd);
      continue;
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.u32 = static_cast<uint32_t>(i);
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      ::close(fd);
      continue;
    }
    cs[i].fd = fd;
  }

  std::vector<char> outBuf(msgSize, 'x');
  std::vector<char> inBuf(1 << 16);
  const int kMaxEvents = 1024;
  std::vector<epoll_event> events(kMaxEvents);

  uint64_t totalBytes = 0;
  uint64_t totalMsgs = 0;
  uint64_t errors = 0;
  uint64_t everConnected = 0;

  struct rusage ru0 {};
  ::getrusage(RUSAGE_SELF, &ru0);
  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::seconds(duration);

  while (true) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      break;
    auto remainMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            .count();
    int timeout = remainMs > 1000 ? 1000 : static_cast<int>(remainMs);

    int n = ::epoll_wait(epfd, events.data(), kMaxEvents, timeout);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("epoll_wait");
      break;
    }

    for (int e = 0; e < n; ++e) {
      uint32_t idx = events[e].data.u32;
      if (idx >= static_cast<uint32_t>(conns))
        continue;
      Conn &c = cs[idx];
      if (c.fd < 0)
        continue;
      uint32_t evs = events[e].events;

      if (evs & (EPOLLERR | EPOLLHUP)) {
        ++errors;
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, c.fd, nullptr);
        ::close(c.fd);
        c.fd = -1;
        continue;
      }

      // 连接完成：检查 SO_ERROR，之后开始发第一个 ping
      if (c.connecting) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 ||
            err != 0) {
          ++errors;
          ::epoll_ctl(epfd, EPOLL_CTL_DEL, c.fd, nullptr);
          ::close(c.fd);
          c.fd = -1;
          continue;
        }
        c.connecting = false;
        c.wantWrite = true;
        c.wpos = 0;
        c.rpos = 0;
        ++everConnected;
      }

      // 写一个完整 ping
      if (c.wantWrite && (evs & EPOLLOUT)) {
        while (c.wpos < msgSize) {
          ssize_t w = ::write(c.fd, outBuf.data() + c.wpos, msgSize - c.wpos);
          if (w > 0) {
            c.wpos += static_cast<size_t>(w);
          } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
          } else {
            ++errors;
            break;
          }
        }
        if (c.wpos >= msgSize) {
          // ping 写完，只等 pong
          c.wantWrite = false;
          epoll_event ev{};
          ev.events = EPOLLIN;
          ev.data.u32 = idx;
          ::epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
        }
      }

      // 读回 pong，收满则发下一个 ping
      if (evs & EPOLLIN) {
        bool needMod = false;
        while (true) {
          ssize_t r = ::read(c.fd, inBuf.data(), inBuf.size());
          if (r > 0) {
            totalBytes += static_cast<uint64_t>(r);
            c.rpos += static_cast<size_t>(r);
          } else if (r == 0) {
            ++errors;
            ::epoll_ctl(epfd, EPOLL_CTL_DEL, c.fd, nullptr);
            ::close(c.fd);
            c.fd = -1;
            break;
          } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          } else {
            ++errors;
            ::epoll_ctl(epfd, EPOLL_CTL_DEL, c.fd, nullptr);
            ::close(c.fd);
            c.fd = -1;
            break;
          }
          if (c.rpos >= msgSize) {
            totalMsgs += c.rpos / msgSize;
            c.rpos %= msgSize;
            c.wantWrite = true;
            c.wpos = 0;
            needMod = true;
            break;
          }
        }
        if (needMod && c.fd >= 0) {
          epoll_event ev{};
          ev.events = EPOLLIN | EPOLLOUT;
          ev.data.u32 = idx;
          ::epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
        }
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  struct rusage ru1 {};
  ::getrusage(RUSAGE_SELF, &ru1);
  double wall = std::chrono::duration<double>(end - start).count();
  double cpu = (ru1.ru_utime.tv_sec + ru1.ru_utime.tv_usec / 1e6 +
                ru1.ru_stime.tv_sec + ru1.ru_stime.tv_usec / 1e6) -
               (ru0.ru_utime.tv_sec + ru0.ru_utime.tv_usec / 1e6 +
                ru0.ru_stime.tv_sec + ru0.ru_stime.tv_usec / 1e6);

  for (auto &c : cs) {
    if (c.fd >= 0) {
      ::epoll_ctl(epfd, EPOLL_CTL_DEL, c.fd, nullptr);
      ::close(c.fd);
    }
  }
  ::close(epfd);

  double mib = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
  fprintf(stderr,
          "[client] conns=%d size=%zu duration=%.2fs\n"
          "[client] bytes=%.1fMiB throughput=%.1fMiB/s msgs=%.0f/s "
          "connected=%llu errors=%llu cpu=%.1fs(%.1f%%)\n",
          conns, msgSize, wall, mib, mib / wall,
          static_cast<double>(totalMsgs) / wall,
          static_cast<unsigned long long>(everConnected),
          static_cast<unsigned long long>(errors), cpu,
          wall > 0 ? cpu / wall * 100.0 : 0.0);
  return 0;
}
