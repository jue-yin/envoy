#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.h"
#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.validate.h"
#include "contrib/http_dubbo_transcoder/filters/http/source/config.h"
#include "contrib/http_dubbo_transcoder/filters/http/source/dubbo_transcoder_filter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpDubboTranscoder {

TEST(HttpDubboTranscodeFilterFactoryTest, HttpDubboTranscodeFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
url_unescape_spec: ALL_CHARACTERS_EXCEPT_RESERVED
request_validation_options:
  reject_unknown_query_parameters: true
services_mapping:
- name: "common.sayHello"
  version: "0.0.0"
  method_mapping:
    name: "sayHello"
    path_matcher:
      match_pattern: "/mytest.service/sayHello"
      match_http_method_spec: ALL_GET
    parameter_mapping:
    - extract_key_spec: ALL_QUERY_PARAMETER
      extract_key: my_param
      mapping_type: "java.lang.String"
)EOF";

  envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  HttpDubboTranscodeFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpDubboTranscodeFilterFactoryTest, HttpDubboTranscodePerFilterCorrectYaml) {
  const std::string yaml_string = R"EOF(
url_unescape_spec: ALL_CHARACTERS_EXCEPT_RESERVED
request_validation_options:
  reject_unknown_query_parameters: true
services_mapping:
- name: "common.sayHello"
  version: "0.0.0"
  method_mapping:
    name: "sayHello"
    path_matcher:
      match_pattern: "/mytest.service/sayHello"
      match_http_method_spec: ALL_GET
    parameter_mapping: 
    - extract_key_spec: ALL_QUERY_PARAMETER
      extract_key: my_param
      mapping_type: "java.lang.String"
)EOF";

  envoy::extensions::filters::http::http_dubbo_transcoder::v3::HttpDubboTranscoder proto_config;
  TestUtility::loadFromYaml(yaml_string, proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  HttpDubboTranscodeFilterFactory factory;
  auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getStrictValidationVisitor());
  const auto* config = dynamic_cast<const DubboTranscoderConfig*>(route_config.get());
  EXPECT_FALSE(config->disabled());
}

} // namespace HttpDubboTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
