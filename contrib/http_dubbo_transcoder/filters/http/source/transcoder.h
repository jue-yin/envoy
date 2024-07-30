#pragma once

#include <vector>

#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.h"
#include "contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3/http_dubbo_transcoder.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "include/nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpDubboTranscoder {

using Status = absl::Status;

/***
 * the Rpc invocation meta represent
 */

struct VariableBinding {
  // the location the params in the dubbo request arg
  std::vector<std::string> field_path;
  // The value to be inserted.
  std::string value;
};
using VariableBindingVecPtr = std::unique_ptr<std::vector<VariableBinding>>;

using TypeAndFiledPath = std::pair<std::string, std::vector<std::string>>;

struct MethodInfo {
  std::string service_name_;
  std::string service_version_;
  std::string service_group_;
  std::string name_;
  std::string match_http_method_;
  std::string match_pattern_;
  Protobuf::RepeatedPtrField<envoy::extensions::filters::http::http_dubbo_transcoder::v3::
                                 HttpDubboTranscoder_DubboMethodMapping_ParameterMapping>
      parameter_mapping_;
  Protobuf::RepeatedPtrField<std::string> attachment_from_header_keys_;
  absl::optional<bool> passthrough_all_headers_;
  absl::optional<Protobuf::RepeatedPtrField<std::string>> passthrough_header_keys_;
  bool passthrough_body_{false};
};

using MethodInfoSharedPtr = std::shared_ptr<MethodInfo>;

} // namespace HttpDubboTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
