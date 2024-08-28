#include "common/sampling.cpp"
#include "common/common.cpp"
#include "common/json-schema-to-grammar.cpp"
#include "common/grammar-parser.cpp"
#include "utils.hpp"
#include "contrib/llm_inference/filters/http/source/inference/inference_context.h"
#include <cstdio>
#include <llama.h>
#include <memory>

char const *LLAMA_COMMIT = "";
char const *LLAMA_COMPILER = "";
char const *LLAMA_BUILD_TARGET = "";
int LLAMA_BUILD_NUMBER = 1;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

gpt_params params;

struct server_slot {
    int id;
    int id_task = -1;

    struct slot_params params;

    slot_state state = SLOT_STATE_IDLE;
    slot_command command = SLOT_COMMAND_NONE;

    // used to determine the slot that has been used the longest
    int64_t t_last_used = -1;

    // generation props
    int32_t n_ctx       = 0;  // context size per slot
    int32_t n_past      = 0;
    int32_t n_decoded   = 0;
    int32_t n_remaining = -1;
    int32_t i_batch     = -1;
    int32_t n_predict   = -1; // TODO: disambiguate from params.n_predict

    int32_t n_prompt_tokens           = 0;
    int32_t n_prompt_tokens_processed = 0;

    json prompt;

    // when a task is submitted, we first tokenize the prompt and store it here
    std::vector<llama_token> prompt_tokens;

    std::string generated_text;
    std::vector<llama_token> cache_tokens;
    std::vector<completion_token_output> generated_token_probs;

    bool infill         = false;
    bool embedding      = false;
    bool has_next_token = true;
    bool truncated      = false;
    bool stopped_eos    = false;
    bool stopped_word   = false;
    bool stopped_limit  = false;

    bool oaicompat = false;

    std::string oaicompat_model;
    std::string stopping_word;

    // sampling
    llama_token sampled;
    struct llama_sampling_params sparams;
    llama_sampling_context * ctx_sampling = nullptr;
    json json_schema;

    int32_t ga_i = 0;   // group-attention state
    int32_t ga_n = 1;   // group-attention factor
    int32_t ga_w = 512; // group-attention width

    int32_t n_past_se = 0; // self-extend

    // stats
    size_t n_sent_text = 0; // number of sent text character
    size_t n_sent_token_probs = 0;

    int64_t t_start_process_prompt;
    int64_t t_start_generation;

    double t_prompt_processing; // ms
    double t_token_generation; // ms

    void reset() {
        n_prompt_tokens    = 0;
        generated_text     = "";
        truncated          = false;
        stopped_eos        = false;
        stopped_word       = false;
        stopped_limit      = false;
        stopping_word      = "";
        n_past             = 0;
        n_sent_text        = 0;
        n_sent_token_probs = 0;
        infill             = false;
        ga_i               = 0;
        n_past_se          = 0;

        generated_token_probs.clear();
    }

    bool has_budget(gpt_params &global_params) {
        if (params.n_predict == -1 && global_params.n_predict == -1) {
            return true; // limitless
        }

        n_remaining = -1;

        if (params.n_predict != -1) {
            n_remaining = params.n_predict - n_decoded;
        } else if (global_params.n_predict != -1) {
            n_remaining = global_params.n_predict - n_decoded;
        }

        return n_remaining > 0; // no budget
    }

    bool available() const {
        return state == SLOT_STATE_IDLE && command == SLOT_COMMAND_NONE;
    }

    bool is_processing() const {
        return (state == SLOT_STATE_IDLE && command == SLOT_COMMAND_LOAD_PROMPT) || state == SLOT_STATE_PROCESSING;
    }

    void add_token_string(const completion_token_output & token) {
        if (command == SLOT_COMMAND_RELEASE) {
            return;
        }
        generated_token_probs.push_back(token);
    }

    void release() {
        if (state == SLOT_STATE_PROCESSING) {
            t_token_generation = (ggml_time_us() - t_start_generation) / 1e3;
            command = SLOT_COMMAND_RELEASE;
        }
    }

    json get_formated_timings() const {
        return json {
            {"prompt_n",               n_prompt_tokens_processed},
            {"prompt_ms",              t_prompt_processing},
            {"prompt_per_token_ms",    t_prompt_processing / n_prompt_tokens_processed},
            {"prompt_per_second",      1e3 / t_prompt_processing * n_prompt_tokens_processed},

            {"predicted_n",            n_decoded},
            {"predicted_ms",           t_token_generation},
            {"predicted_per_token_ms", t_token_generation / n_decoded},
            {"predicted_per_second",   1e3 / t_token_generation * n_decoded},
        };
    }

    size_t find_stopping_strings(const std::string & text, const size_t last_token_size, const stop_type type) {
        size_t stop_pos = std::string::npos;

        for (const std::string & word : params.antiprompt) {
            size_t pos;

            if (type == STOP_TYPE_FULL) {
                const size_t tmp      = word.size() + last_token_size;
                const size_t from_pos = text.size() > tmp ? text.size() - tmp : 0;

                pos = text.find(word, from_pos);
            } else {
                pos = find_partial_stop_string(word, text);
            }

            if (pos != std::string::npos && (stop_pos == std::string::npos || pos < stop_pos)) {
                if (type == STOP_TYPE_FULL) {
                    stopped_word   = true;
                    stopping_word  = word;
                    has_next_token = false;
                }
                stop_pos = pos;
            }
        }

        return stop_pos;
    }

    void print_timings() const {
        char buffer[512];

        double t_token = t_prompt_processing / n_prompt_tokens_processed;
        double n_tokens_second = 1e3 / t_prompt_processing * n_prompt_tokens_processed;

        snprintf(buffer, 512, "prompt eval time     = %10.2f ms / %5d tokens (%8.2f ms per token, %8.2f tokens per second)",
                t_prompt_processing, n_prompt_tokens_processed,
                t_token, n_tokens_second);

        LOG_INFO(buffer, {
            {"id_slot",                   id},
            {"id_task",                   id_task},
            {"t_prompt_processing",       t_prompt_processing},
            {"n_prompt_tokens_processed", n_prompt_tokens_processed},
            {"t_token",                   t_token},
            {"n_tokens_second",           n_tokens_second},
        });

        t_token = t_token_generation / n_decoded;
        n_tokens_second = 1e3 / t_token_generation * n_decoded;

        snprintf(buffer, 512, "generation eval time = %10.2f ms / %5d runs   (%8.2f ms per token, %8.2f tokens per second)",
                t_token_generation, n_decoded,
                t_token, n_tokens_second);

        LOG_INFO(buffer, {
            {"id_slot",            id},
            {"id_task",            id_task},
            {"t_token_generation", t_token_generation},
            {"n_decoded",          n_decoded},
            {"t_token",            t_token},
            {"n_tokens_second",    n_tokens_second},
        });

        snprintf(buffer, 512, "          total time = %10.2f ms", t_prompt_processing + t_token_generation);

        LOG_INFO(buffer, {
            {"id_slot",             id},
            {"id_task",             id_task},
            {"t_prompt_processing", t_prompt_processing},
            {"t_token_generation",  t_token_generation},
            {"t_total",             t_prompt_processing + t_token_generation},
        });
    }
};

struct server_task {
    int id        = -1; 
    int id_target = -1;

    server_task_type type;
    json data;

    bool infill    = false;
    bool embedding = false;
};

/* ================================================================= */
/* Constructors */
/* ================================================================= */

InferenceContext::InferenceContext(Singleton::InstanceSharedPtr owner, InferenceThread& inference_thread, 
          const ModelParameter& model_parameter, const std::string& model_path, const ModelChosen& model_chosen):owner_(owner),
           inference_thread_(inference_thread), model_name_(model_chosen.model_name) {
  loadModel(model_parameter, model_path, model_chosen);
}

/* ================================================================= */
/* Destructors */
/* ================================================================= */

InferenceContext::~InferenceContext() {
  llama_kv_cache_clear(ctx);
  if (ctx) {
    llama_free(ctx);
    ctx = nullptr;
  }

  if (model) {
    llama_free_model(model);
    model = nullptr;
  }

  // Clear any sampling context
  for (server_slot & slot : slots) {
    if (slot.ctx_sampling != nullptr) {
        llama_sampling_free(slot.ctx_sampling);
    }
  }
  llama_batch_free(batch);
  llama_backend_free();
}

/* ================================================================= */
/* get task id */
/* ================================================================= */

int InferenceContext::getId() {
  return inference_thread_.getId();
}

/* ================================================================= */
/* load model */
/* ================================================================= */

bool InferenceContext::loadModel(const ModelParameter& model_parameter, const std::string& model_path, const ModelChosen& model_chosen) {
  params.n_threads = model_parameter.n_threads;
  params.n_parallel = model_parameter.n_parallel;
  params.embedding = model_chosen.embedding;
  
  params.model = model_path;
  
  gpt_params_handle_model_default(params);

  if (params.model_alias == "unknown") {
    params.model_alias = params.model;
  }
  llama_backend_init();
  llama_numa_init(params.numa);

  // load the model
  {
    // dedicate one sequence to the system prompt
    params.n_parallel += 1;
    llama_init_result llama_init = llama_init_from_gpt_params(params);
    model = llama_init.model;
    ctx = llama_init.context;
    params.n_parallel -= 1; // but be sneaky about it
    if (model == nullptr) {
      return false;
    }
    n_ctx = llama_n_ctx(ctx);

    add_bos_token = llama_add_bos_token(model);
    has_eos_token = !llama_add_eos_token(model);
  }
  // init slot
  {
    const int32_t n_ctx_slot = n_ctx / params.n_parallel;

    LOG_INFO("initializing slots", {{"n_slots", params.n_parallel}});

    for (int i = 0; i < params.n_parallel; i++) {
      server_slot slot;

      slot.id = i;
      slot.n_ctx = n_ctx_slot;
      slot.n_predict = params.n_predict;

      LOG_INFO("new slot", {
        {"id_slot",    slot.id},
        {"n_ctx_slot", slot.n_ctx}
      });

      const int ga_n = params.grp_attn_n;
      const int ga_w = params.grp_attn_w;
      if (ga_n != 1) {
        GGML_ASSERT(ga_n > 0                    && "ga_n must be positive");                       // NOLINT
        GGML_ASSERT(ga_w % ga_n == 0            && "ga_w must be a multiple of ga_n");             // NOLINT
        LOG_INFO("slot self-extend", {
          {"id_slot", slot.id},
          {"ga_n",    ga_n},
          {"ga_w",    ga_w}
        });
      }
      slot.ga_i = 0;
      slot.ga_n = ga_n;
      slot.ga_w = ga_w;

      slot.reset();

      slots.push_back(slot);

      // the update_slots() logic will always submit a maximum of n_batch tokens
      // note that n_batch can be > n_ctx (e.g. for non-causal attention models such as BERT where the KV cache is not used)
      {
        const int32_t n_batch = llama_n_batch(ctx);

        // only a single seq_id per token is needed
        batch = llama_batch_init(std::max(n_batch, params.n_parallel), 0, 1);
      }
    }
  }

  LOG_INFO("model loaded", {});

  // if a custom chat template is not supplied, we will use the one that comes with the model (if any)
  {
    llama_chat_message chat[] = {{"user", "test"}};

    if (!(llama_chat_apply_template(model, nullptr, chat, 1, true, nullptr, 0) > 0)) {
      chat_template_ = "chatml";
    }
  }
  return true;
}

/* ================================================================= */
/* Preparation for model inference,
   After preparation, asynchronous thread will be called to handle inference tasks */
/* ================================================================= */

void InferenceContext::modelInference(LookupBodyCallback&& cb, std::shared_ptr<InferenceTaskMetaData>&& task_meta_data, int& inference_timeout) {
  callback_body_[task_meta_data->id] = std::move(cb);
  completion_id_ = gen_chatcmplid();
  inference_timeout_ = inference_timeout * 1e6;
  server_task task;
  task.id = task_meta_data->id;
  task.id_target = task_meta_data->id_target;
  task.infill = task_meta_data->infill;
  try {
    task.data = json::parse(task_meta_data->data);
  } catch (const std::exception &) {
    sendError(task.id, "request data is wrong", ERROR_TYPE_INVALID_REQUEST);
    return;
  }

  switch (task_meta_data->type) {
    case InferencetasktypeTypeCompletion:
    {
      is_openai_ = true;
      task.data = oaicompat_completion_params_parse(model, task.data, chat_template_);
      task.type = SERVER_TASK_TYPE_COMPLETION;
      task.embedding = false;
    } break;
    case InferencetasktypeTypeEmbeedings:
    {
      task.embedding = true;
      is_openai_ = false;
      // an input prompt can be a string or a list of tokens (integer)
      json prompt;
      if (task.data.count("input") != 0) {
          is_openai_ = true;
          prompt = task.data.at("input");
      } else if (task.data.count("content") != 0) {
          // with "content", we only support single prompt
          prompt = std::vector<std::string>{task.data.at("content")};
      } else {
          sendError(task.id, "input or content must be provided", ERROR_TYPE_INVALID_REQUEST);
          return;
      }
      task.data = json{{"prompt", prompt}};
      task.type = SERVER_TASK_TYPE_COMPLETION;
    } break;
    case InferencetasktypeTypeCancel:
    {
      task.type = SERVER_TASK_TYPE_CANCEL;
      break;
    }
  }

  inference_thread_.addTask([this, task](){
    this->processSingleTask(task);
  });
}

/* ================================================================= */
/* handle inference tasks,
   and assign slot to the task */
/* ================================================================= */

void InferenceContext::processSingleTask(const server_task & task) {
  switch (task.type) {
    case SERVER_TASK_TYPE_COMPLETION:
    {
      server_slot * slot = nullptr;
      std::string prompt;
      if (task.data.contains("prompt") && task.data.at("prompt").is_string()) {
          prompt = json_value(task.data, "prompt", std::string());
      }

      slot = getAvailableSlot(prompt);

      if (slot == nullptr) {
        // if no slot is available, we defer this task for processing later
        inference_thread_.addTask([this, task](){
          this->processSingleTask(task);
        });
        return;
      }

      if (!slot->available()) {
        // if this slot isn't available, we defer this task for processing later
        inference_thread_.addTask([this, task](){
          this->processSingleTask(task);
        });
        return;
      }

      slot->reset();

      slot->id_task   = task.id;
      slot->infill    = task.infill;
      slot->embedding = task.embedding;
      if (!launchSlotWithTask(*slot, task)) {
        return;
      }
    } break;
    case SERVER_TASK_TYPE_CANCEL:
    {
      // release slot linked with the task id
      for (auto & use_slot : slots) {
        if (use_slot.id_task == task.id_target) {
          use_slot.release();
          break;
        }
      }
    } break;
    case SERVER_TASK_TYPE_NEXT_RESPONSE:
    {
      // do nothing
    } break;
  }
  updateSlots();
}

server_slot * InferenceContext::getAvailableSlot(const std::string & prompt) {
  server_slot * ret = nullptr;
  // find the slot that has at least n% prompt similarity
  if (ret == nullptr && slot_prompt_similarity != 0.0f && !prompt.empty()) {
    int max_lcp_len = 0;
    float similarity = 0;

    for (server_slot & slot : slots) {
      // skip the slot if it is not available
      if (!slot.available()) {
        continue;
      }

      // skip the slot if it does not contains prompt
      if (!slot.prompt.is_string()) {
          continue;
      }

      // current slot's prompt
      std::string slot_prompt = slot.prompt.get<std::string>();

      // length of the current slot's prompt
      int slot_prompt_len = slot_prompt.size();

      // length of the Longest Common Prefix between the current slot's prompt and the input prompt
      int lcp_len = common_part(slot_prompt, prompt);

      // fraction of the common substring length compared to the current slot's prompt length
      similarity = static_cast<float>(lcp_len) / slot_prompt_len;

      // select the current slot if the criteria match
      if (lcp_len > max_lcp_len && similarity > slot_prompt_similarity) {
        max_lcp_len = lcp_len;
        ret = &slot;
      }
    }
  }

  // find the slot that has been least recently used
  if (ret == nullptr) {
    int64_t t_last = ggml_time_us();
    for (server_slot & slot : slots) {
      // skip the slot if it is not available
      if (!slot.available()) {
        continue;
      }

      // select the current slot if the criteria match
      if (slot.t_last_used < t_last) {
        t_last = slot.t_last_used;
        ret = &slot;
      }
    }
  }
  return ret;
}

bool InferenceContext::launchSlotWithTask(server_slot & slot, const server_task & task) {
  slot_params default_params;
  llama_sampling_params default_sparams;
  auto & data = task.data;

  if (data.count("__oaicompat") != 0) {
    slot.oaicompat = true;
    slot.oaicompat_model = json_value(data, "model", std::string(DEFAULT_OAICOMPAT_MODEL));
  } else {
    slot.oaicompat = false;
    slot.oaicompat_model = "";
  }
  slot.params.stream             = json_value(data, "stream",            false);
  slot.params.cache_prompt       = json_value(data, "cache_prompt",      false);
  slot.params.n_predict          = json_value(data, "n_predict",         default_params.n_predict);
  slot.sparams.top_k             = json_value(data, "top_k",             default_sparams.top_k);
  slot.sparams.top_p             = json_value(data, "top_p",             default_sparams.top_p);
  slot.sparams.min_p             = json_value(data, "min_p",             default_sparams.min_p);
  slot.sparams.tfs_z             = json_value(data, "tfs_z",             default_sparams.tfs_z);
  slot.sparams.typical_p         = json_value(data, "typical_p",         default_sparams.typical_p);
  slot.sparams.temp              = json_value(data, "temperature",       default_sparams.temp);
  slot.sparams.dynatemp_range    = json_value(data, "dynatemp_range",    default_sparams.dynatemp_range);
  slot.sparams.dynatemp_exponent = json_value(data, "dynatemp_exponent", default_sparams.dynatemp_exponent);
  slot.sparams.penalty_last_n    = json_value(data, "repeat_last_n",     default_sparams.penalty_last_n);
  slot.sparams.penalty_repeat    = json_value(data, "repeat_penalty",    default_sparams.penalty_repeat);
  slot.sparams.penalty_freq      = json_value(data, "frequency_penalty", default_sparams.penalty_freq);
  slot.sparams.penalty_present   = json_value(data, "presence_penalty",  default_sparams.penalty_present);
  slot.sparams.mirostat          = json_value(data, "mirostat",          default_sparams.mirostat);
  slot.sparams.mirostat_tau      = json_value(data, "mirostat_tau",      default_sparams.mirostat_tau);
  slot.sparams.mirostat_eta      = json_value(data, "mirostat_eta",      default_sparams.mirostat_eta);
  slot.sparams.penalize_nl       = json_value(data, "penalize_nl",       default_sparams.penalize_nl);
  slot.params.n_keep             = json_value(data, "n_keep",            slot.params.n_keep);
  slot.params.n_discard          = json_value(data, "n_discard",         default_params.n_discard);
  slot.sparams.seed              = json_value(data, "seed",              default_sparams.seed);
  slot.sparams.n_probs           = json_value(data, "n_probs",           default_sparams.n_probs);
  slot.sparams.min_keep          = json_value(data, "min_keep",          default_sparams.min_keep);

  // process "json_schema" and "grammar"
  if (data.contains("json_schema") && !data.at("json_schema").is_null() && data.contains("grammar") && !data.at("grammar").is_null()) {
    sendError(task.id, "Either \"json_schema\" or \"grammar\" can be specified, but not both", ERROR_TYPE_INVALID_REQUEST);
    return false;
  } else if (data.contains("json_schema") && !data.contains("grammar")) {
    try {
      auto schema                = json_value(data, "json_schema", json::object());
      slot.sparams.grammar       = json_schema_to_grammar(schema);
    } catch (const std::exception & e) {
      sendError(task.id, std::string("\"json_schema\": ") + e.what(), ERROR_TYPE_INVALID_REQUEST);
      return false;
    }
  } else {
    slot.sparams.grammar       = json_value(data, "grammar",           default_sparams.grammar);
  }

  if (slot.params.cache_prompt && slot.ga_n != 1) {
    slot.params.cache_prompt = false;
  }

  if (slot.n_predict > 0 && slot.params.n_predict > slot.n_predict) {
    slot.params.n_predict = slot.n_predict;
  }
  
  // infill
  slot.params.input_prefix = json_value(data, "input_prefix", default_params.input_prefix);
  slot.params.input_suffix = json_value(data, "input_suffix", default_params.input_suffix);

  // get prompt
  {
    const auto & prompt = data.find("prompt");
    if (prompt == data.end()) {
      sendError(task.id, "Either \"prompt\" or \"messages\" must be provided", ERROR_TYPE_INVALID_REQUEST);
      return false;
    } else {
      slot.prompt = *prompt;
    }
    if (slot.prompt.is_array() && slot.prompt.empty()) {
      sendError(task.id, "\"prompt\" cannot be an empty array", ERROR_TYPE_INVALID_REQUEST);
      return false;
    }
  }

  // penalize user-provided tokens
  {
    slot.sparams.penalty_prompt_tokens.clear();
    slot.sparams.use_penalty_prompt_tokens = false;

    const auto & penalty_prompt = data.find("penalty_prompt");

    if (penalty_prompt != data.end()) {
      if (penalty_prompt->is_string()) {
        const auto penalty_prompt_string = penalty_prompt->get<std::string>();
        slot.sparams.penalty_prompt_tokens = llama_tokenize(model, penalty_prompt_string, false);

        if (slot.params.n_predict > 0) {
          slot.sparams.penalty_prompt_tokens.reserve(slot.sparams.penalty_prompt_tokens.size() + slot.params.n_predict);
        }
        slot.sparams.use_penalty_prompt_tokens = true;
      }
      else if (penalty_prompt->is_array()) {
        const auto n_tokens = penalty_prompt->size();
        slot.sparams.penalty_prompt_tokens.reserve(n_tokens + std::max(0, slot.params.n_predict));

        const int n_vocab = llama_n_vocab(model);
        for (const auto & penalty_token : *penalty_prompt) {
          if (penalty_token.is_number_integer()) {
            const auto tok = penalty_token.get<llama_token>();
            if (tok >= 0 && tok < n_vocab) {
              slot.sparams.penalty_prompt_tokens.push_back(tok);
            }
          }
        }
        slot.sparams.use_penalty_prompt_tokens = true;
      }
    }
  }

  {
    slot.sparams.logit_bias.clear();

    if (json_value(data, "ignore_eos", false)) {
      slot.sparams.logit_bias[llama_token_eos(model)] = -INFINITY;
    }

    const auto & logit_bias = data.find("logit_bias");
    if (logit_bias != data.end() && logit_bias->is_array()) {
      const int n_vocab = llama_n_vocab(model);
      for (const auto & el : *logit_bias) {
        // TODO: we may want to throw errors here, in case "el" is incorrect
        if (el.is_array() && el.size() == 2) {
          float bias;
          if (el[1].is_number()) {
            bias = el[1].get<float>();
          } else if (el[1].is_boolean() && !el[1].get<bool>()) {
            bias = -INFINITY;
          } else {
            continue;
          }

          if (el[0].is_number_integer()) {
            llama_token tok = el[0].get<llama_token>();
            if (tok >= 0 && tok < n_vocab) {
              slot.sparams.logit_bias[tok] = bias;
            }
          } else if (el[0].is_string()) {
            auto toks = llama_tokenize(model, el[0].get<std::string>(), false);
            for (auto tok : toks) {
              slot.sparams.logit_bias[tok] = bias;
            }
          }
        }
      }
    }
  }

  {
    slot.params.antiprompt.clear();

    const auto & stop = data.find("stop");
    if (stop != data.end() && stop->is_array()) {
      for (const auto & word : *stop) {
        if (!word.empty()) {
          slot.params.antiprompt.push_back(word);
        }
      }
    }
  }

  {
    const auto & samplers_sequence = data.find("samplers");
    if (samplers_sequence != data.end() && samplers_sequence->is_array()) {
      std::vector<std::string> sampler_names;
      for (const auto & sampler_name : *samplers_sequence) {
        if (sampler_name.is_string()) {
          sampler_names.emplace_back(sampler_name);
        }
      }
      slot.sparams.samplers_sequence = llama_sampling_types_from_names(sampler_names, false);
    } else {
      slot.sparams.samplers_sequence = default_sparams.samplers_sequence;
    }
  }

  {
    if (slot.ctx_sampling != nullptr) {
      llama_sampling_free(slot.ctx_sampling);
    }
    slot.ctx_sampling = llama_sampling_init(slot.sparams);
    if (slot.ctx_sampling == nullptr) {
      // for now, the only error that may happen here is invalid grammar
      sendError(task.id, "Failed to parse grammar", ERROR_TYPE_INVALID_REQUEST);
      return false;
    }
  }

  slot.command = SLOT_COMMAND_LOAD_PROMPT;
  slot.prompt_tokens.clear();

  return true;
}

/* ================================================================= */
/* do the hard job, use llama.cpp api to inference */
/* ================================================================= */

std::vector<llama_token> tokenize(llama_context *ctx, const json & json_prompt, bool add_special) {
  // TODO: currently, we tokenize using special tokens by default
  //       this is not always correct (see https://github.com/ggerganov/llama.cpp/pull/4160#issuecomment-1824826216)
  //       but it's better compared to completely ignoring ChatML and other chat templates
  const bool TMP_FORCE_SPECIAL = true;

  // If `add_bos` is true, we only add BOS, when json_prompt is a string,
  // or the first element of the json_prompt array is a string.
  std::vector<llama_token> prompt_tokens;

  if (json_prompt.is_array()) {
    bool first = true;
    for (const auto & p : json_prompt) {
      if (p.is_string()) {
        auto s = p.template get<std::string>();

        std::vector<llama_token> p;
        if (first) {
          p = ::llama_tokenize(ctx, s, add_special, TMP_FORCE_SPECIAL);
          first = false;
        } else {
          p = ::llama_tokenize(ctx, s, false, TMP_FORCE_SPECIAL);
        }

        prompt_tokens.insert(prompt_tokens.end(), p.begin(), p.end());
      } else {
        if (first) {
          first = false;
        }

        prompt_tokens.push_back(p.template get<llama_token>());
      }
    }
  } else {
    auto s = json_prompt.template get<std::string>();
    prompt_tokens = ::llama_tokenize(ctx, s, add_special, TMP_FORCE_SPECIAL);
  }

  return prompt_tokens;
}

bool InferenceContext::processToken(completion_token_output & result, server_slot & slot) {
  // remember which tokens were sampled - used for repetition penalties during sampling
  const std::string token_str = llama_token_to_piece(ctx, result.tok, false);
  slot.sampled = result.tok;

  // search stop word and delete it
  slot.generated_text += token_str;
  slot.has_next_token = true;

  if (slot.ctx_sampling->params.use_penalty_prompt_tokens && result.tok != -1) {
    // we can change penalty_prompt_tokens because it is always created from scratch each request
    slot.ctx_sampling->params.penalty_prompt_tokens.push_back(result.tok);
  }

  // check if there is incomplete UTF-8 character at the end
  bool incomplete = false;
  for (unsigned i = 1; i < 5 && i <= slot.generated_text.size(); ++i) {
    unsigned char c = slot.generated_text[slot.generated_text.size() - i];
    if ((c & 0xC0) == 0x80) {
      // continuation byte: 10xxxxxx
      continue;
    }
    if ((c & 0xE0) == 0xC0) {
      // 2-byte character: 110xxxxx ...
      incomplete = i < 2;
    } else if ((c & 0xF0) == 0xE0) {
      // 3-byte character: 1110xxxx ...
      incomplete = i < 3;
    } else if ((c & 0xF8) == 0xF0) {
      // 4-byte character: 11110xxx ...
      incomplete = i < 4;
    }
    // else 1-byte character or invalid byte
    break;
  }

  if (!incomplete) {
    size_t pos = std::min(slot.n_sent_text, slot.generated_text.size());

    const std::string str_test = slot.generated_text.substr(pos);
    bool is_stop_full = false;

    size_t stop_pos = slot.find_stopping_strings(str_test, token_str.size(), STOP_TYPE_FULL);
    if (stop_pos != std::string::npos) {
      is_stop_full = true;
      slot.generated_text.erase(
        slot.generated_text.begin() + pos + stop_pos,
        slot.generated_text.end());
      pos = std::min(slot.n_sent_text, slot.generated_text.size());
    } else {
      is_stop_full = false;
      stop_pos = slot.find_stopping_strings(str_test, token_str.size(), STOP_TYPE_PARTIAL);
    }

    // check if there is any token to predict
    if (stop_pos == std::string::npos || (!slot.has_next_token && !is_stop_full && stop_pos > 0)) {
      // no send the stop word in the response
      result.text_to_send = slot.generated_text.substr(pos, std::string::npos);
      slot.n_sent_text += result.text_to_send.size();
      // add the token to slot queue and cache
    }

    slot.add_token_string(result);
    if (slot.params.stream) {
      sendPartialResponse(result, slot);
    }
  }

  if (incomplete) {
    slot.has_next_token = true;
  }

  // check the limits
  if (slot.n_decoded > 0 && slot.has_next_token && !slot.has_budget(params)) {
    slot.stopped_limit  = true;
    slot.has_next_token = false;
  }

  if (ggml_time_us() - slot.t_start_generation > inference_timeout_) {
    slot.stopped_limit  = true;
    slot.has_next_token = false;
  }

  if (llama_token_is_eog(model, result.tok)) {
    slot.stopped_eos    = true;
    slot.has_next_token = false;
  }

  auto n_ctx_train = llama_n_ctx_train(model);
  if (slot.params.n_predict < 1 && slot.n_predict < 1 && slot.ga_n == 1
        && slot.n_prompt_tokens + slot.n_decoded >= n_ctx_train) {
    slot.truncated      = true;
    slot.stopped_limit  = true;
    slot.has_next_token = false; // stop prediction
  }

  return slot.has_next_token; // continue
}

void InferenceContext::updateSlots() {
  // release slots
  for (auto & slot : slots) {
    if (slot.command == SLOT_COMMAND_RELEASE) {
      slot.state       = SLOT_STATE_IDLE;
      slot.command     = SLOT_COMMAND_NONE;
      slot.t_last_used = ggml_time_us();
    }
  }

  // check if all slots are idle
  {
    bool all_idle = true;

    for (auto & slot : slots) {
      if (slot.state != SLOT_STATE_IDLE || slot.command != SLOT_COMMAND_NONE) {
        all_idle = false;
        break;
      }
    }

    if (all_idle) {
      LOG_INFO("all slots are idle", {});
      if (system_prompt.empty() && clean_kv_cache) {
        // clear the entire KV cache
        llama_kv_cache_clear(ctx);
      }
      return;
    }
  }
  {
    server_task task;
    task.type      = SERVER_TASK_TYPE_NEXT_RESPONSE;
    task.id_target = -1;
    task.id = getId();
    inference_thread_.addTask([this, task](){
      this->processSingleTask(task);
    });
  }

  // apply context-shift if needed
  // TODO: simplify and improve
  for (server_slot & slot : slots) {
    if (slot.ga_n == 1) {
      if (slot.is_processing() && static_cast<int>(system_tokens.size()) + slot.n_past >= slot.n_ctx - 1) {
        // Shift context
        const int n_keep    = slot.params.n_keep + add_bos_token;
        const int n_left    = static_cast<int>(system_tokens.size()) + slot.n_past - n_keep;
        const int n_discard = slot.params.n_discard ? slot.params.n_discard : (n_left / 2);

        LOG_INFO("slot context shift", {
            {"id_slot",         slot.id},
            {"id_task",         slot.id_task},
            {"n_keep",          n_keep},
            {"n_left",          n_left},
            {"n_discard",       n_discard},
            {"n_ctx",           n_ctx},
            {"n_past",          slot.n_past},
            {"n_system_tokens", system_tokens.size()},
            {"n_cache_tokens",  slot.cache_tokens.size()}
        });

        llama_kv_cache_seq_rm (ctx, slot.id + 1, n_keep            , n_keep + n_discard);
        llama_kv_cache_seq_add(ctx, slot.id + 1, n_keep + n_discard, system_tokens.size() + slot.n_past, -n_discard);

        if (slot.params.cache_prompt) {
          for (size_t i = n_keep + n_discard; i < slot.cache_tokens.size(); i++) {
              slot.cache_tokens[i - n_discard] = slot.cache_tokens[i];
          }

          slot.cache_tokens.resize(slot.cache_tokens.size() - n_discard);
        }

        slot.n_past -= n_discard;

        slot.truncated = true;
      }
    }
  }
  // start populating the batch for this iteration
  llama_batch_clear(batch);

  // frist, add sampled tokens from any ongoing sequences
  for (auto & slot : slots) {
    if (slot.state == SLOT_STATE_IDLE) {
      continue;
    }
    slot.i_batch = batch.n_tokens;

    const int32_t slot_npast = slot.n_past_se > 0 ? slot.n_past_se : slot.n_past;

    // TODO: we always have to take into account the "system_tokens"
    //       this is not great and needs to be improved somehow
    llama_batch_add(batch, slot.sampled, system_tokens.size() + slot_npast, { slot.id + 1 }, true);

    slot.n_past += 1;

    if (slot.params.cache_prompt) {
      slot.cache_tokens.push_back(slot.sampled);
    }
  }

  // process in chunks of params.n_batch

  int32_t n_batch  = llama_n_batch(ctx);
  int32_t n_ubatch = llama_n_ubatch(ctx);

  // next, batch any pending prompts without exceeding n_batch
    for (auto & slot : slots) {
      // this slot still has a prompt to be processed
      if (slot.state == SLOT_STATE_IDLE && slot.command == SLOT_COMMAND_LOAD_PROMPT) {
        auto & prompt_tokens = slot.prompt_tokens;

        // we haven't tokenized the prompt yet - do it now:
        if (prompt_tokens.empty()) {

          slot.t_start_process_prompt = ggml_time_us();
          slot.t_start_generation = 0;

          if (slot.infill) {
            bool suff_rm_leading_spc = true;
            // if (params.input_suffix.find_first_of(' ') == 0 && params.input_suffix.size() > 1) {
            //   params.input_suffix.erase(0, 1);
            //   suff_rm_leading_spc = false;
            // }

            auto prefix_tokens = tokenize(ctx, slot.params.input_prefix, false);
            auto suffix_tokens = tokenize(ctx, slot.params.input_suffix, false);

            const int space_token = 29871; // TODO: this should not be hardcoded
            if (suff_rm_leading_spc && !suffix_tokens.empty() && suffix_tokens[0] == space_token) {
              suffix_tokens.erase(suffix_tokens.begin());
            }

            prefix_tokens.insert(prefix_tokens.begin(), llama_token_prefix(model));
            prefix_tokens.insert(prefix_tokens.begin(), llama_token_bos(model)); // always add BOS
            prefix_tokens.insert(prefix_tokens.end(),   llama_token_suffix(model));
            prefix_tokens.insert(prefix_tokens.end(),   suffix_tokens.begin(), suffix_tokens.end());
            prefix_tokens.push_back(llama_token_middle(model));
            prompt_tokens = prefix_tokens;
          } else {
            prompt_tokens = tokenize(ctx, slot.prompt, system_prompt.empty()); // add BOS if there isn't system prompt
          }

          slot.n_past = 0;
          slot.n_prompt_tokens = prompt_tokens.size();

          // empty prompt passed -> release the slot and send empty response
          if (prompt_tokens.empty()) {
            LOG_INFO("empty prompt - releasing slot", {
                      {"id_slot", slot.id},
                      {"id_task", slot.id_task}
            });
            slot.state = SLOT_STATE_PROCESSING;
            slot.command = SLOT_COMMAND_NONE;
            slot.release();
            slot.print_timings();
            sendFinalResponse(slot);
            continue;
          }

          if (slot.embedding) {
            // this prompt is too large to process - discard it
            if (slot.n_prompt_tokens > n_ubatch) {
                slot.state = SLOT_STATE_PROCESSING;
                slot.command = SLOT_COMMAND_NONE;
                slot.release();
                sendError(slot.id_task, "input is too large to process. increase the physical batch size", ERROR_TYPE_SERVER);
                continue;
            }
          } else {
            if (slot.params.n_keep < 0) {
              slot.params.n_keep = slot.n_prompt_tokens;
            }
            slot.params.n_keep = std::min(slot.n_ctx - 4, slot.params.n_keep);

            // if input prompt is too big, truncate it (if group attention self-extend is disabled)
            if (slot.ga_n == 1 && slot.n_prompt_tokens >= slot.n_ctx) {
              const int n_left = slot.n_ctx - slot.params.n_keep;

              const int n_block_size = n_left / 2;
              const int erased_blocks = (slot.n_prompt_tokens - slot.params.n_keep - n_block_size) / n_block_size;

              std::vector<llama_token> new_tokens(
                      prompt_tokens.begin(),
                      prompt_tokens.begin() + slot.params.n_keep);

              new_tokens.insert(
                      new_tokens.end(),
                      prompt_tokens.begin() + slot.params.n_keep + erased_blocks * n_block_size,
                      prompt_tokens.end());

              prompt_tokens = std::move(new_tokens);

              slot.truncated = true;
              slot.n_prompt_tokens = prompt_tokens.size();

              GGML_ASSERT(slot.n_prompt_tokens < slot.n_ctx);
            }

            llama_sampling_reset(slot.ctx_sampling);

            if (!slot.params.cache_prompt) {
              slot.n_past_se = 0;
              slot.ga_i      = 0;
            } else {
              GGML_ASSERT(slot.ga_n == 1);

              // reuse any previously computed tokens that are common with the new prompt
              slot.n_past = common_part(slot.cache_tokens, prompt_tokens);

              // push the prompt into the sampling context (do not apply grammar)
              for (int i = 0; i < slot.n_past; ++i) {
                llama_sampling_accept(slot.ctx_sampling, ctx, slot.cache_tokens[i], false);
              }
            }
          }

          if (slot.n_past == slot.n_prompt_tokens && slot.n_past > 0) {
            // we have to evaluate at least 1 token to generate logits.
            LOG_INFO("we have to evaluate at least 1 token to generate logits", {
                { "id_slot", slot.id },
                { "id_task", slot.id_task }
            });

            slot.n_past--;
            if (slot.ga_i > 0) {
              slot.n_past_se--;
            }
          }

          slot.n_prompt_tokens_processed = 0;
        }

        if (slot.embedding) {
          // cannot fit the prompt in the current batch - will try next iter
          if (batch.n_tokens + slot.n_prompt_tokens > n_batch) {
            continue;
          }
        }

        // keep only the common part
        int p0 = static_cast<int>(system_tokens.size()) + slot.n_past;
        if (!llama_kv_cache_seq_rm(ctx, slot.id + 1, p0, -1)) {
          // could not partially delete (likely using a non-Transformer model)
          llama_kv_cache_seq_rm(ctx, slot.id + 1, -1, -1);

          p0 = static_cast<int>(system_tokens.size());
          if (p0 != 0) {
            // copy over the system prompt when there is one
            llama_kv_cache_seq_cp(ctx, 0, slot.id + 1, -1, -1);
          }

          // there is no common part left (except for the system prompt)
          slot.n_past = 0;
          slot.n_past_se = 0;
          slot.ga_i = 0;
          // TODO: is the system prompt ever in the sampling context?
          llama_sampling_reset(slot.ctx_sampling);
        }

        // remove the non-common part from the cache
        slot.cache_tokens.resize(slot.n_past);
        LOG_INFO("kv cache rm [p0, end)", {
            { "id_slot", slot.id },
            { "id_task", slot.id_task },
            { "p0",      p0 }
        });
        int32_t slot_npast = slot.n_past_se > 0 ? slot.n_past_se : slot.n_past;

        int32_t ga_i = slot.ga_i;
        int32_t ga_n = slot.ga_n;
        int32_t ga_w = slot.ga_w;

        // add prompt tokens for processing in the current batch
        // TODO: the self-extend stuff here is a mess - simplify and/or abstract it somehow
        for (; slot.n_past < slot.n_prompt_tokens && batch.n_tokens < n_batch; ++slot.n_past) {
          if (slot.ga_n != 1) {
            while (slot_npast >= ga_i + ga_w) {
              const int bd = (ga_w/ga_n)*(ga_n - 1);
              slot_npast -= bd;
              ga_i += ga_w/ga_n;
            }
          }

          llama_batch_add(batch, prompt_tokens[slot.n_past], system_tokens.size() + slot_npast, { slot.id + 1 }, false);

          if (slot.params.cache_prompt) {
            slot.cache_tokens.push_back(prompt_tokens[slot.n_past]);
          }

          slot.n_prompt_tokens_processed++;
          slot_npast++;
        }

        // entire prompt has been processed - start decoding new tokens
        if (slot.n_past == slot.n_prompt_tokens) {
          slot.state   = SLOT_STATE_PROCESSING;
          slot.command = SLOT_COMMAND_NONE;

          GGML_ASSERT(batch.n_tokens > 0);

          // extract the logits only for the last token
          batch.logits[batch.n_tokens - 1] = true;

          slot.n_decoded = 0;
          slot.i_batch   = batch.n_tokens - 1;
        }
      }

      if (batch.n_tokens >= n_batch) {
        break;
      }
    }

  if (batch.n_tokens == 0) {
    return;
  }
  // process the created batch of tokens

  for (int32_t i = 0; i < batch.n_tokens; i += n_batch) {
    const int32_t n_tokens = std::min(n_batch, batch.n_tokens - i);
    for (auto & slot : slots) {
      if (slot.ga_n != 1) {
        // context extension via Self-Extend
        // TODO: simplify and/or abstract this
        while (slot.n_past_se >= slot.ga_i + slot.ga_w) {
          const int ib = (slot.ga_n * slot.ga_i) / slot.ga_w;
          const int bd = (slot.ga_w / slot.ga_n) * (slot.ga_n - 1);
          const int dd = (slot.ga_w / slot.ga_n) - ib * bd - slot.ga_w;

          LOG_TEE("\n");
          LOG_TEE("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n", slot.ga_i, slot.n_past_se, ib * bd, slot.ga_i + ib * bd, slot.n_past_se + ib * bd);
          LOG_TEE("div:   [%6d, %6d] / %6d -> [%6d, %6d]\n", slot.ga_i + ib * bd, slot.ga_i + ib * bd + slot.ga_w, slot.ga_n, (slot.ga_i + ib * bd) / slot.ga_n, (slot.ga_i + ib * bd + slot.ga_w) / slot.ga_n);
          LOG_TEE("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n", slot.ga_i + ib * bd + slot.ga_w, slot.n_past_se + ib * bd, dd, slot.ga_i + ib * bd + slot.ga_w + dd, slot.n_past_se + ib * bd + dd);

          llama_kv_cache_seq_add(ctx, slot.id + 1, slot.ga_i, slot.n_past_se, ib * bd);
          llama_kv_cache_seq_div(ctx, slot.id + 1, slot.ga_i + ib * bd, slot.ga_i + ib * bd + slot.ga_w, slot.ga_n);
          llama_kv_cache_seq_add(ctx, slot.id + 1, slot.ga_i + ib * bd + slot.ga_w, slot.n_past_se + ib * bd, dd);

          slot.n_past_se -= bd;

          slot.ga_i += slot.ga_w / slot.ga_n;

          LOG_TEE("\nn_past_old = %d, n_past = %d, ga_i = %d\n\n", slot.n_past_se + bd, slot.n_past_se, slot.ga_i);
        }

        slot.n_past_se += n_tokens;
      }
    }

    llama_batch batch_view =
    {
      n_tokens,
      batch.token    + i,
      nullptr,
      batch.pos      + i,
      batch.n_seq_id + i,
      batch.seq_id   + i,
      batch.logits   + i,
      0, 0, 0, // unused
    };

    const int ret = llama_decode(ctx, batch_view);
  
    if (ret != 0) {
      if (n_batch == 1 || ret < 0) {

        for (auto & slot : slots) {
          slot.state = SLOT_STATE_PROCESSING;
          slot.command = SLOT_COMMAND_NONE;
          slot.release();
          sendError(slot.id_task, "Input prompt is too big compared to KV size. Please try increasing KV size.", ERROR_TYPE_SERVER);
        }
        break; // break loop of n_batch
      }

      // retry with half the batch size to try to find a free slot in the KV cache
      n_batch /= 2;
      i -= n_batch;

      continue; // continue loop of n_batch
    }

    for (auto & slot : slots) {
      if (slot.state != SLOT_STATE_PROCESSING || slot.i_batch < static_cast<int>(i) || slot.i_batch >= static_cast<int>(i + n_tokens)) {
        continue; // continue loop of slots
      }

      // prompt evaluated for embedding
      if (slot.embedding) {
        sendEmbedding(slot, batch_view);
        slot.release();
        slot.i_batch = -1;
        continue; // continue loop of slots
      }

      completion_token_output result;

      const llama_token id = llama_sampling_sample(slot.ctx_sampling, ctx, nullptr, slot.i_batch - i);

      llama_sampling_accept(slot.ctx_sampling, ctx, id, true);
      
      slot.n_decoded += 1;
      if (slot.n_decoded == 1) {
          slot.t_start_generation = ggml_time_us();
          slot.t_prompt_processing = (slot.t_start_generation - slot.t_start_process_prompt) / 1e3;
      }

      llama_token_data_array cur_p = { slot.ctx_sampling->cur.data(), slot.ctx_sampling->cur.size(), false };
      result.tok = id;
                
      const size_t n_probs = std::min(cur_p.size, static_cast<size_t>(slot.sparams.n_probs));
      if (n_probs > 0) {
        const size_t n_valid = slot.ctx_sampling->n_valid;

        // Make sure at least n_probs top tokens are at the front of the vector:
        if (slot.sparams.temp == 0.0f && n_probs > n_valid) {
          llama_sample_top_k(ctx, &cur_p, n_probs, 0);
        }

        if (slot.sparams.temp == 0.0f) {
          // With greedy sampling the probabilities have possibly not been calculated.
          for (size_t i = 0; i < n_probs; ++i) {
            result.probs.push_back({
              cur_p.data[i].id,
              i == 0 ? 1.0f : 0.0f
            });
          }
        } else {
          for (size_t i = 0; i < n_probs; ++i) {
            result.probs.push_back({
              cur_p.data[i].id,
              i >= n_valid ? 0.0f : cur_p.data[i].p // Tokens filtered out due to e.g. top_k have 0 probability.
            });
          }
        }
      }

      if (!processToken(result, slot)) {
        slot.release();
        slot.print_timings();
        sendFinalResponse(slot);
      }

      slot.i_batch = -1;
    }
  }

}

/* ================================================================= */
/*
The top part mainly does the work of loading models and doing model inference, 
and the bottom part mainly does the work of sending generated tokens.
*/
/* ================================================================= */

void InferenceContext::sendPartialResponse(completion_token_output& tkn, server_slot& slot) {
  json res;
  res     = json {
    {"content",    tkn.text_to_send},
    {"stop",       false},
    {"id_slot",    slot.id},
    {"multimodal", false}
  };

  if (slot.sparams.n_probs > 0) {
    const std::vector<llama_token> to_send_toks = llama_tokenize(ctx, tkn.text_to_send, false);
    const size_t probs_pos      = std::min(slot.n_sent_token_probs,                       slot.generated_token_probs.size());
    const size_t probs_stop_pos = std::min(slot.n_sent_token_probs + to_send_toks.size(), slot.generated_token_probs.size());

    std::vector<completion_token_output> probs_output;
    if (probs_pos < probs_stop_pos) {
      probs_output = std::vector<completion_token_output>(
          slot.generated_token_probs.begin() + probs_pos,
          slot.generated_token_probs.begin() + probs_stop_pos);
    }
    slot.n_sent_token_probs = probs_stop_pos;

    res["completion_probabilities"] = probs_vector_to_json(ctx, probs_output);
  }

  if (slot.oaicompat) {
    res["oaicompat_token_ctr"] = slot.n_decoded;
    res["model"] = slot.oaicompat_model;
  }

  if (is_openai_) {
    std::vector<json> result_array = format_partial_response_oaicompat(res, completion_id_);
    for (auto it = result_array.begin(); it != result_array.end(); ++it) {
      if (!it->empty()) {
        const std::string str =
            "data: " +
            it->dump(-1, ' ', false, json::error_handler_t::replace) +
            "\n\n";
        if (callback_body_.find(slot.id_task) != callback_body_.end()) {
          LookupBodyCallback& cb = callback_body_[slot.id_task];
          cb(ModelInferenceResult{true, false, str, NO_ERROR});
        }
      }
    }
  } else {
    const std::string str =
        "data: " +
        res.dump(-1, ' ', false, json::error_handler_t::replace) +
        "\n\n";
    if (callback_body_.find(slot.id_task) != callback_body_.end()) {
      LookupBodyCallback& cb = callback_body_[slot.id_task];
      cb(ModelInferenceResult{true, false, str,NO_ERROR});
    }
  }
}

void InferenceContext::sendFinalResponse(server_slot & slot) {
  json res;
  res = json {
    {"content",             !slot.params.stream ? slot.generated_text : ""},
    {"id_slot",             slot.id},
    {"stop",                true},
    {"model",               model_name_},
    {"tokens_predicted",    slot.n_decoded},
    {"tokens_evaluated",    slot.n_prompt_tokens},
    // {"generation_settings", get_formated_generation(slot)},
    {"prompt",              slot.prompt},
    {"truncated",           slot.truncated},
    {"stopped_eos",         slot.stopped_eos},
    {"stopped_word",        slot.stopped_word},
    {"stopped_limit",       slot.stopped_limit},
    {"stopping_word",       slot.stopping_word},
    {"tokens_cached",       slot.n_past},
    {"timings",             slot.get_formated_timings()}
  };


  if (slot.sparams.n_probs > 0) {
    std::vector<completion_token_output> probs;
    if (!slot.params.stream && slot.stopped_word) {
      const std::vector<llama_token> stop_word_toks = llama_tokenize(ctx, slot.stopping_word, false);

      size_t safe_offset = std::min(slot.generated_token_probs.size(), stop_word_toks.size());
      probs = std::vector<completion_token_output>(
              slot.generated_token_probs.begin(),
              slot.generated_token_probs.end() - safe_offset);
    } else {
      probs = std::vector<completion_token_output>(
            slot.generated_token_probs.begin(),
            slot.generated_token_probs.end());
    }

    res["completion_probabilities"] = probs_vector_to_json(ctx, probs);
  }

  if (slot.oaicompat) {
    res["oaicompat_token_ctr"] = slot.n_decoded;
    res["model"] = slot.oaicompat_model;
  }


  if (is_openai_) {
    res = format_final_response_oaicompat(model_name_, res, completion_id_);
  } 
  if (callback_body_.find(slot.id_task) != callback_body_.end()) {
    LookupBodyCallback& cb = callback_body_[slot.id_task];
    cb(ModelInferenceResult{true, true, res.dump(-1, ' ', false, json::error_handler_t::replace), NO_ERROR});
  }
}

void InferenceContext::sendEmbedding(server_slot & slot, const llama_batch & batch) {
  json res;
  const int n_embd = llama_n_embd(model);

  std::vector<float> embd_res(n_embd, 0.0f);

  for (int i = 0; i < batch.n_tokens; ++i) {
    if (!batch.logits[i] || batch.seq_id[i][0] != slot.id + 1) {
      continue;
    }

    const float * embd = llama_get_embeddings_seq(ctx, batch.seq_id[i][0]);
    if (embd == nullptr) {
      embd = llama_get_embeddings_ith(ctx, i);
    }

    if (embd == nullptr) {
      res = json {
          {"embedding", std::vector<float>(n_embd, 0.0f)},
      };

      continue;
    }

    llama_embd_normalize(embd, embd_res.data(), n_embd);

    res = json {
        {"embedding", embd_res},
    };
  }

  json responses;
  if (res.count("results")) {
    // result for multi-task
    responses = res.at("results");
  } else {
    // result for single task
    responses = std::vector<json>{res};
  }

  // write JSON response
  json root = is_openai_
      ? format_embeddings_response_oaicompat(model_name_, responses)
      : responses[0];

  if (callback_body_.find(slot.id_task) != callback_body_.end()) {
    LookupBodyCallback& cb = callback_body_[slot.id_task];
    cb(ModelInferenceResult{true, true, root.dump(-1, ' ', false, json::error_handler_t::replace), NO_ERROR});
  }
}

void InferenceContext::sendError(const int& id_task, const std::string & error, const enum error_type type = ERROR_TYPE_SERVER) {
  if (callback_body_.find(id_task) != callback_body_.end()) {
    LookupBodyCallback& cb = callback_body_[id_task];
    cb(ModelInferenceResult{false, false, error,type});
  }
}

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
