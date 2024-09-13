#pragma once

#include "contrib/envoy/extensions/filters/http/llm_inference/v3/llm_inference.pb.h"
#include "contrib/envoy/extensions/filters/http/llm_inference/v3/llm_inference.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LLMInference {

/**
 * Config registration for the inference filter. @see NamedHttpFilterConfigFactory.
 */
class LLMInferenceFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::llm_inference::v3::modelParameter,
                                 envoy::extensions::filters::http::llm_inference::v3::modelChosen>  {
public:
  LLMInferenceFilterConfigFactory() : FactoryBase("envoy.filters.http.llm_inference") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::llm_inference::v3::modelParameter& proto_config,
      const std::string&,
      Server::Configuration::FactoryContext&) override;
  
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::llm_inference::v3::modelChosen& proto_config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
  
};

} // namespace LLMInference
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
