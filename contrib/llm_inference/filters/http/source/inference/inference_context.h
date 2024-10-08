#pragma once

#include "contrib/llm_inference/filters/http/source/inference/inference_thread.h"
#include "contrib/llm_inference/filters/http/source/inference/inference_task.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/common/common/logger.h"
#include "common/common.h"
#include "llama.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

struct server_task;
class server_slot;
struct completion_token_output;

struct ModelInferenceResult {
  bool inference_successed = false;
  bool stopped = false;
  std::string ss;
  error_type type;
};

using LookupBodyCallback = std::function<void(ModelInferenceResult&&)>;

class InferenceContext: public Logger::Loggable<Logger::Id::llm_inference> {
public:

  InferenceContext(Envoy::Singleton::InstanceSharedPtr, InferenceThread&, const std::string&);
  ~InferenceContext();
  bool loadLLM(const ModelParameter&, const std::string&);
  bool loadEmbedding(const ModelParameter&, const std::string&);
  void modelInference(LookupBodyCallback&& cb, std::shared_ptr<InferenceTaskMetaData>&&, int&);
  int getId();

private:

  server_slot * getAvailableSlot(const std::string &);
  bool launchSlotWithTask(server_slot &, const server_task &);
  void updateSlots();
  void processSingleTask(const server_task &);
  bool processToken(completion_token_output &, server_slot &);
  void sendPartialResponse(completion_token_output&, server_slot &);
  void sendFinalResponse(server_slot &);
  void sendEmbedding(server_slot &, const llama_batch &);
  void sendError(const int &, const std::string &, const enum error_type);
  
  const Envoy::Singleton::InstanceSharedPtr owner_;
  InferenceThread& inference_thread_;
  absl::flat_hash_map<int, LookupBodyCallback> callback_body_;
  std::string model_name_;

  llama_model * model = nullptr;
  llama_context * ctx = nullptr;
  llama_batch batch;
  bool clean_kv_cache = true;
  bool add_bos_token  = true;
  bool has_eos_token = true;
  int32_t n_ctx; // total context for all clients / slots
  gpt_params params;

  // system prompt
  std::string              system_prompt;
  std::vector<llama_token> system_tokens;

  // slots / clients
  std::vector<server_slot> slots;

  // Necessary similarity of prompt for slot selection
  float slot_prompt_similarity = 0.0f;

  std::string chat_template_ = "";
  std::string completion_id_;
  bool is_openai_;
  int64_t inference_timeout_;
};

using InferenceContextSharedPtr = std::shared_ptr<InferenceContext>;
using InferenceContextHashMapSharedPtr = std::shared_ptr<absl::flat_hash_map<std::string, InferenceContextSharedPtr>>;

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy