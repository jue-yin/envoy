#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "envoy/thread/thread.h"

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

struct server_task;

class InferenceThread{
public:
  InferenceThread(Thread::ThreadFactory& thread_factory);
  ~InferenceThread();

  /**
   * Adds the given inference task.
   */
  void addTask(std::function<void(void)>);

  /**
   * get the inference task id.
   */
  int getId();

  /**
   * Signals the inference thread that it's time to check the current task
   * and perform if necessary.
   */
  void signal();

private:
  /**
   * The function that runs on the thread.
   */
  void work();

  /**
   * @return false if terminating, true if `signalled_` is true or the run-again period
   * has passed.
   */
  bool waitForSignal();

  /**
   * Notifies the thread to terminate.
   */
  void terminate();

  absl::Mutex mu_ ABSL_ACQUIRED_BEFORE(tasks_mu_);
  bool signalled_ ABSL_GUARDED_BY(mu_) = false;
  bool terminating_ ABSL_GUARDED_BY(mu_) = false;

  absl::Mutex tasks_mu_ ABSL_ACQUIRED_BEFORE(mu_);
  std::vector<std::function<void(void)>> tasks_ ABSL_GUARDED_BY(tasks_mu_);

  Thread::ThreadPtr thread_;

  std::function<void(void)>                callback_context_function_;
  
  int id_ ABSL_GUARDED_BY(id_mu_) = false;
  absl::Mutex id_mu_;

};


} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
