#include "contrib/http_dubbo_transcoder/filters/http/source/config.h"

#include "contrib/http_dubbo_transcoder/filters/http/source/dubbo_transcoder_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpDubboTranscoder {

Http::FilterFactoryCb HttpDubboTranscodeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  DubboTranscoderConfigSharedPtr config =
      std::make_shared<DubboTranscoderConfig>(proto_config, DUBBO_STATS_PREFIX, context.scope());
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TranscodeFilter>(*config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
HttpDubboTranscodeFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<DubboTranscoderConfig>(proto_config, DUBBO_STATS_PREFIX, context.scope());
};

REGISTER_FACTORY(HttpDubboTranscodeFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace HttpDubboTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
