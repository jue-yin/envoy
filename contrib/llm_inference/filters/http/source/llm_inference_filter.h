#pragma once

#include <string>

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "contrib/envoy/extensions/filters/http/llm_inference/v3/llm_inference.pb.h"
#include "contrib/llm_inference/filters/http/source/inference/inference_context.h"
#include "contrib/llm_inference/filters/http/source/inference/inference_task.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

using ModelPath = Protobuf::Map<std::string, std::string>;

class LLMInferenceFilterConfig : public Router::RouteSpecificFilterConfig  {
public:
  LLMInferenceFilterConfig(const envoy::extensions::filters::http::llm_inference::v3::modelParameter& proto_config);

  const ModelParameter& modelParameter() const {return model_parameter_;}
  const ModelPath& chatModelPath() const {return chat_modelpath_; }
  const ModelPath& embeddingModelPath() const {return embedding_modelpath_; }

private:
  const ModelParameter model_parameter_;
  const ModelPath chat_modelpath_;
  const ModelPath embedding_modelpath_;
};

using LLMInferenceFilterConfigSharedPtr = std::shared_ptr<LLMInferenceFilterConfig>;

class LLMInferenceFilterConfigPerRoute : public Router::RouteSpecificFilterConfig  {
public:
  LLMInferenceFilterConfigPerRoute(const envoy::extensions::filters::http::llm_inference::v3::modelChosen& proto_config);

  const ModelChosen& modelChosen() const {return model_chosen_;};

private:
  const ModelChosen model_chosen_;
};

using LLMInferenceFilterConfigPerRouteSharedPtr = std::shared_ptr<LLMInferenceFilterConfigPerRoute>;

class LLMInferenceFilter : public Http::PassThroughDecoderFilter,
                           public std::enable_shared_from_this<LLMInferenceFilter> {
public:
  LLMInferenceFilter(LLMInferenceFilterConfigSharedPtr, InferenceContextHashMapSharedPtr);
  ~LLMInferenceFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  void getHeaders(std::shared_ptr<InferenceTaskMetaData>&&);
  void onBody(ModelInferenceResult&&);

private:
  const LLMInferenceFilterConfigSharedPtr config_;
  const InferenceContextHashMapSharedPtr ctx_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Event::TimerPtr timer_;
  InferenceTaskType task_type_;
  Buffer::InstancePtr request_data_;
  std::string model_name_;
  int first_byte_timeout_ = 10;
  int inference_timeout_ = 90;
  int id_task_ = -1;
  bool header_ = false;
};

using LLMInferenceFilterSharedPtr = std::shared_ptr<LLMInferenceFilter>;
using LLMInferenceFilterWeakPtr = std::weak_ptr<LLMInferenceFilter>;

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy