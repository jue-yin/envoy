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

  std::shared_ptr<InferenceContext> load(std::shared_ptr<InferenceSingleton> singleton, const ModelParameter& model_parameter,
              const ModelChosen& model_chosen, const std::string& model_path) {
    std::shared_ptr<InferenceContext> ctx;
    absl::MutexLock lock(&mu_);
    auto it = ctx_.find(model_chosen.model_name);
    if (it != ctx_.end()) {
      ctx = it->second.lock();
    }
    if (!ctx) {
      ctx = std::make_shared<InferenceContext>(singleton, inference_thread_, model_parameter, model_path, model_chosen);
      ctx_[model_chosen.model_name] = ctx;
    }
    return ctx;
  }

private:
  InferenceThread inference_thread_;
  absl::Mutex mu_;
  absl::flat_hash_map<std::string, std::weak_ptr<InferenceContext>> ctx_ ABSL_GUARDED_BY(mu_);
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

    InferenceContextSharedPtr ctx;
    auto modelpath = config->modelPath();
    if (modelpath.contains(model_Chosen_.model_name)) {
      ctx = inference->load(inference, config->modelParameter(), model_Chosen_, modelpath[model_Chosen_.model_name]);
    }

    return [config, ctx](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<LLMInferenceFilter>(config, ctx));
    };
}


Router::RouteSpecificFilterConfigConstSharedPtr LLMInferenceFilterConfigFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::llm_inference::v3::modelChosen& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
    LLMInferenceFilterConfigPerRouteSharedPtr config = 
        std::make_shared<LLMInferenceFilterConfigPerRoute>(LLMInferenceFilterConfigPerRoute(proto_config));
    
    model_Chosen_ = config->modelChosen();
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