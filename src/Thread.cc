#include <CurrentThread.h>
#include <Thread.h>

#include <cstdio>
#include <memory>
#include <semaphore.h>
#include <thread>

std::atomic_int Thread::numCreated_(0);
// std::atomic_int Thread::numCreated_;
Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false), joined_(false), tid_(0), func_(std::move(func)),
      name_(name) {
  setDefaultName();
}
Thread::~Thread() {
  if (started_ && !joined_) {
    thread_->detach();
  }
}
void Thread::start() {
  started_ = true;
  sem_t sem;
  sem_init(&sem, false, 0);
  thread_ = std::shared_ptr<std::thread>(new std::thread([&] {
    tid_ = CurrentThread::tid();
    sem_post(&sem);
    func_();
  }));
  sem_wait(&sem);
}
void Thread::join() {
  joined_ = true;
  thread_->join();
}
void Thread::setDefaultName() {
  int num = numCreated_++;

  if (name_.empty()) {
    char buf[64];
    snprintf(buf, sizeof(buf), "thread%d",
             num); // TODO:numCreated_    =>   Call to deleted constructor of
                   // 'std::atomic_int' (aka 'atomic<int>')
    name_ = buf;
  }
}
