#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/header_parser.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"
#include "contrib/envoy/http/active_redirect_policy.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of InternalActiveRedirectPolicyImpl that reads from the proto
 * InternalActiveRedirectPolicyImpl of the RouteAction.
 */
class InternalActiveRedirectPolicyImpl : public InternalActiveRedirectPolicy,
                                         Logger::Loggable<Logger::Id::config> {
public:
  // Constructor that enables internal redirect with policy_config controlling the configurable
  // behaviors.
  explicit InternalActiveRedirectPolicyImpl(
      const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config,
      ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name);
  explicit InternalActiveRedirectPolicyImpl(
      const envoy::config::route::v3::InternalActiveRedirectPolicy::RedirectPolicy& policy_config,
      ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name);
  // Default constructor that disables internal redirect.
  InternalActiveRedirectPolicyImpl() = default;

  bool enabled() const override { return enabled_; }

  bool shouldRedirectForResponseCode(const Http::Code& response_code) const override {
    return redirect_response_codes_.contains(response_code);
  }

  std::vector<InternalRedirectPredicateSharedPtr> predicates() const override;

  uint32_t maxInternalRedirects() const override { return max_internal_redirects_; }

  bool isCrossSchemeRedirectAllowed() const override { return allow_cross_scheme_redirect_; }

  void evaluateHeaders(Http::HeaderMap& headers,
                       const StreamInfo::StreamInfo* stream_info) const override;

  std::string redirectUrl(absl::optional<std::string> current_path = absl::nullopt) const override;

  bool forcedUseOriginalHost() const override;
  bool forcedAddHeaderBeforeRouteMatcher() const override {
    return forced_add_header_before_route_matcher_;
  }

private:
  absl::flat_hash_set<Http::Code> buildRedirectResponseCodes(
      const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config) const;
  absl::flat_hash_set<Http::Code> buildRedirectResponseCodes(
      const envoy::config::route::v3::InternalActiveRedirectPolicy::RedirectPolicy& policy_config)
      const;

  const std::string current_route_name_;
  const absl::flat_hash_set<Http::Code> redirect_response_codes_;
  const uint32_t max_internal_redirects_{1};
  const bool enabled_{false};
  const bool allow_cross_scheme_redirect_{false};
  const std::string redirect_url_;
  const HeaderParserPtr request_headers_parser_;
  const Regex::CompiledMatcherPtr redirect_url_rewrite_regex_;
  const std::string redirect_url_rewrite_regex_substitution_;
  const std::string host_rewrite_;
  const bool forced_use_original_host_{false};
  const bool forced_add_header_before_route_matcher_{false};

  std::vector<std::pair<InternalRedirectPredicateFactory*, ProtobufTypes::MessagePtr>>
      predicate_factories_;
};

using InternalActiveRedirectPolicySharedPtr = std::shared_ptr<InternalActiveRedirectPolicyImpl>;
using ActiveRedirectPolicies = std::vector<InternalActiveRedirectPolicySharedPtr>;
using DefaultInternalActiveRedirectPolicy = ConstSingleton<InternalActiveRedirectPolicyImpl>;

class InternalActiveRedirectPoliciesImpl : public InternalActiveRedirectPolicy,
                                           Logger::Loggable<Logger::Id::config> {
public:
  // Constructor that enables internal redirect with policy_config controlling the configurable
  // behaviors.
  explicit InternalActiveRedirectPoliciesImpl(
      const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config,
      ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name);
  // Default constructor that disables internal redirect.
  InternalActiveRedirectPoliciesImpl();

  bool enabled() const override;
  bool shouldRedirectForResponseCode(const Http::Code& response_code) const override;
  std::vector<InternalRedirectPredicateSharedPtr> predicates() const override;
  uint32_t maxInternalRedirects() const override;
  bool isCrossSchemeRedirectAllowed() const override;
  void evaluateHeaders(Http::HeaderMap& headers,
                       const StreamInfo::StreamInfo* stream_info) const override;
  std::string redirectUrl(absl::optional<std::string> current_path = absl::nullopt) const override;
  bool forcedUseOriginalHost() const override;
  bool forcedAddHeaderBeforeRouteMatcher() const override;

private:
  ActiveRedirectPolicies policies_;
  mutable ActiveRedirectPolicies::size_type current_policy_index_{0};
};

} // namespace Router
} // namespace Envoy
