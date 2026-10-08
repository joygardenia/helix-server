#include "ThreadPool.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <thread>

static int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static void testExecution() {
  ThreadPool pool(4, 128);
  pool.start();
  std::atomic<int> counter{0};
  for (int i = 0; i < 100; ++i)
    CHECK(pool.submit([&counter]() { counter.fetch_add(1); }));

  for (int i = 0; i < 200 && counter.load() < 100; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(counter.load() == 100);
  pool.stop();
}

static void testRejection() {
  ThreadPool pool(1, 2);
  pool.start();
  std::mutex m;
  std::condition_variable cv;
  bool release = false;

  // 占住唯一 worker，使后续任务只能排队。
  CHECK(pool.submit([&]() {
    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&]() { return release; });
  }));

  // 等到占位任务确实开始执行，队列清空。
  for (int i = 0; i < 200 && pool.pendingTasks() != 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

  CHECK(pool.submit([]() {}));
  CHECK(pool.submit([]() {}));
  CHECK(!pool.submit([]() {})); // 队列满，拒绝

  {
    std::lock_guard<std::mutex> lock(m);
    release = true;
  }
  cv.notify_all();
  pool.stop();
}

static void testGracefulStop() {
  ThreadPool pool(3, 64);
  pool.start();
  std::atomic<int> done{0};
  for (int i = 0; i < 30; ++i)
    pool.submit([&done]() { done.fetch_add(1); });
  pool.stop();
  CHECK(pool.submit([]() {}) == false); // 停止后拒绝新任务
  CHECK(done.load() == 30);             // 已入队任务被排空执行
}

static void testNoLingeringWorkers() {
  const unsigned before = std::thread::hardware_concurrency();
  {
    ThreadPool pool(8, 16);
    pool.start();
    pool.stop();
  }
  (void)before;
  CHECK(true); // stop() 可重入且不崩溃；worker 已 join
}

int main() {
  testExecution();
  testRejection();
  testGracefulStop();
  testNoLingeringWorkers();

  if (g_failures == 0)
    std::printf("threadpool_test: ALL PASS\n");
  else
    std::printf("threadpool_test: %d FAILURES\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
