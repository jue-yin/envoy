#pragma once

#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.h"
#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpDubboTranscoder {

const std::string DUBBO_STATS_PREFIX = "http_dubbo_transcoder";

/**
 * Config registration for the buffer filter.
 */
class HttpDubboTranscodeFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder> {
public:
  HttpDubboTranscodeFilterFactory() : FactoryBase("envoy.filters.http.http_dubbo_transcoder") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

DECLARE_FACTORY(HttpDubboTranscodeFilterFactory);

} // namespace HttpDubboTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
