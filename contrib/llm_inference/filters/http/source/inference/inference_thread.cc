#include "contrib/llm_inference/filters/http/source/inference/inference_thread.h"

#include "envoy/thread/thread.h"
#include "inference_context.h"
#include <algorithm>
#include <cstdio>
#include <unistd.h>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {
InferenceThread::InferenceThread(Thread::ThreadFactory& thread_factory)
    : thread_(thread_factory.createThread([this]() { work(); })) {}


InferenceThread::~InferenceThread() {
  terminate();
  thread_->join();
}

void InferenceThread::addTask(std::function<void(void)> callback) {
  {
    absl::MutexLock lock(&tasks_mu_);
    tasks_.push_back(std::move(callback));
  }
  // Signal to unblock InferenceThread
  signal();
}

int InferenceThread::getId() {
  {
    absl::MutexLock lock(&id_mu_);
    id_++;
    return id_;
  }
}

void InferenceThread::signal() {
  absl::MutexLock lock(&mu_);
  signalled_ = true;
}

void InferenceThread::terminate() {
  absl::MutexLock lock(&mu_);
  terminating_ = true;
  signalled_ = true;
}

bool InferenceThread::waitForSignal() {
  absl::MutexLock lock(&mu_);
  // Worth noting here that if `signalled_` is already true, the lock is not released
  // until idle_ is false again, so waitForIdle will not return until `signalled_`
  // stays false for the duration of an eviction cycle.
  mu_.Await(absl::Condition(&signalled_));
  signalled_ = false;
  return !terminating_;
}

void InferenceThread::work() {  
  while (waitForSignal()) {
    std::vector<std::function<void(void)>> tasks;
    {
      // Take a local copy of the set of tasks, so we don't hold the lock while
      // work is being performed.
      absl::MutexLock lock(&tasks_mu_);
      tasks = std::move(tasks_);
    }

    for (const std::function<void(void)>& callback_context_function: tasks) {
      callback_context_function();
    }   
  }
}

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
