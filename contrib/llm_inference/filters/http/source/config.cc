#include "contrib/llm_inference/filters/http/source/config.h"

#include "contrib/llm_inference/filters/http/source/llm_inference_filter.h"
#include <string>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

class InferenceSingleton : public Envoy::Singleton::Instance {
public:
  InferenceSingleton(Thread::ThreadFactory& thread_factory)
      : inference_thread_(thread_factory) {}

  std::shared_ptr<InferenceContext> loadLLM(std::shared_ptr<InferenceSingleton> singleton, const ModelParameter& model_parameter,
              const std::string& model_name, const std::string& model_path) {
    std::shared_ptr<InferenceContext> ctx;
    std::string model = model_name + " " + std::to_string(model_parameter.n_threads) + " " + std::to_string(model_parameter.n_parallel);
    auto it = ctx_.find(model);
    if (it != ctx_.end()) {
      ctx = it->second.lock();
    }
    if (!ctx) {
      ctx = std::make_shared<InferenceContext>(singleton, inference_thread_, model_name);
      ctx->loadLLM(model_parameter, model_path);
      ctx_[model] = ctx;
    }
    return ctx;
  }

  std::shared_ptr<InferenceContext> loadEmbedding(std::shared_ptr<InferenceSingleton> singleton, const ModelParameter& model_parameter,
              const std::string& model_name, const std::string& model_path) {
    std::shared_ptr<InferenceContext> ctx;
    std::string model = model_name + " " + std::to_string(model_parameter.n_threads) + " " + std::to_string(model_parameter.n_parallel);
    auto it = ctx_.find(model);
    if (it != ctx_.end()) {
      ctx = it->second.lock();
    }
    if (!ctx) {
      ctx = std::make_shared<InferenceContext>(singleton, inference_thread_, model_name);
      ctx->loadEmbedding(model_parameter, model_path);
      ctx_[model] = ctx;
    }
    return ctx;
  }

private:
  InferenceThread inference_thread_;
  absl::flat_hash_map<std::string, std::weak_ptr<InferenceContext>> ctx_;
};

SINGLETON_MANAGER_REGISTRATION(http_inference_singleton);

Http::FilterFactoryCb LLMInferenceFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::llm_inference::v3::modelParameter& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

    LLMInferenceFilterConfigSharedPtr config =
        std::make_shared<LLMInferenceFilterConfig>(LLMInferenceFilterConfig(proto_config));

    std::shared_ptr<InferenceSingleton> inference =
        context.singletonManager().getTyped<InferenceSingleton>(
            SINGLETON_MANAGER_REGISTERED_NAME(http_inference_singleton), [&context] {
              return std::make_shared<InferenceSingleton>(context.api().threadFactory());
            });

    absl::flat_hash_map<std::string, InferenceContextSharedPtr> ctx;

    auto chat_modelpath = config->chatModelPath();

    for (auto& model: chat_modelpath) {
      ctx[model.first] = inference->loadLLM(inference, config->modelParameter(), model.first, model.second);
    }

    auto embedding_modelpath = config->embeddingModelPath();

    for (auto& model: embedding_modelpath) {
      ctx[model.first] = inference->loadEmbedding(inference, config->modelParameter(), model.first, model.second);
    }
  
    InferenceContextHashMapSharedPtr ctx_map = std::make_shared<absl::flat_hash_map<std::string, InferenceContextSharedPtr>>(ctx);

    return [config, ctx_map](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<LLMInferenceFilter>(config, ctx_map));
    };
}


Router::RouteSpecificFilterConfigConstSharedPtr LLMInferenceFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::llm_inference::v3::modelChosen& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
    LLMInferenceFilterConfigPerRouteSharedPtr config = 
        std::make_shared<LLMInferenceFilterConfigPerRoute>(LLMInferenceFilterConfigPerRoute(proto_config));

    return config;
}

/**
 * Static registration for this llm inference filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LLMInferenceFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy