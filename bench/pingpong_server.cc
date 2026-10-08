// ping-pong 压测服务端：用项目网络库 TcpServer 做 echo
// 用法: ./bin/pingpong_server --port 9000 --threads 1 --duration 0
//   --duration 0 表示常驻；>0 表示跑满 N 秒后自动退出并打印服务端 CPU 占用
#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Buffer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logger.h"
#include "TcpConnection.h"
#include "TcpServer.h"
#include "Timestamp.h"

int main(int argc, char *argv[]) {
  int port = 9000;
  int threads = 1;
  int duration = 0;  // 秒，0 = 常驻

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc)
      port = std::atoi(argv[++i]);
    else if (a == "--threads" && i + 1 < argc)
      threads = std::atoi(argv[++i]);
    else if (a == "--duration" && i + 1 < argc)
      duration = std::atoi(argv[++i]);
  }
  if (threads < 1)
    threads = 1;

  // 压测关闭 INFO 日志，避免日志开销污染吞吐
  Logger::setLogLevel(Logger::ERROR);

  EventLoop loop;
  InetAddress addr(static_cast<uint16_t>(port), "0.0.0.0");
  TcpServer server(&loop, addr, "PingPongServer");
  server.setThreadNum(threads);
  server.setConnectionCallback([](const TcpConnectionPtr &) {});
  server.setMessageCallback(
      [](const TcpConnectionPtr &conn, Buffer *buf, Timestamp) {
        std::string data = buf->retrieveAllAsString();
        conn->send(data);  // echo
      });
  server.start();

  struct rusage ru0 {};
  ::getrusage(RUSAGE_SELF, &ru0);
  double wallStart = Timestamp::now().microSecondsSinceEpoch() / 1e6;

  if (duration > 0) {
    loop.runAfter(static_cast<double>(duration), [&loop]() { loop.quit(); });
  }

  fprintf(stderr, "[server] port=%d threads=%d duration=%d\n", port, threads,
          duration);
  loop.loop();

  struct rusage ru1 {};
  ::getrusage(RUSAGE_SELF, &ru1);
  double wallEnd = Timestamp::now().microSecondsSinceEpoch() / 1e6;
  double cpu = (ru1.ru_utime.tv_sec + ru1.ru_utime.tv_usec / 1e6 +
                ru1.ru_stime.tv_sec + ru1.ru_stime.tv_usec / 1e6) -
               (ru0.ru_utime.tv_sec + ru0.ru_utime.tv_usec / 1e6 +
                ru0.ru_stime.tv_sec + ru0.ru_stime.tv_usec / 1e6);
  double wall = wallEnd - wallStart;
  fprintf(stderr, "[server] wall=%.2fs cpu=%.2fs cpu%%=%.1f\n", wall, cpu,
          wall > 0 ? cpu / wall * 100.0 : 0.0);
  return 0;
}
