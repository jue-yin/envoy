#include "contrib/common/active_redirect/source/active_redirect_policy_impl.h"

#include <memory>
#include <string>

#include "source/common/common/empty_string.h"
#include "source/common/common/regex.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/path_utility.h"

namespace Envoy {
namespace Router {

InternalActiveRedirectPolicyImpl::InternalActiveRedirectPolicyImpl(
    const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name)
    : current_route_name_(current_route_name),
      redirect_response_codes_(buildRedirectResponseCodes(policy_config)),
      max_internal_redirects_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(policy_config, max_internal_redirects, 1)),
      enabled_(true), allow_cross_scheme_redirect_(policy_config.allow_cross_scheme_redirect()),
      redirect_url_(policy_config.redirect_url()),
      request_headers_parser_(HeaderParser::configure(policy_config.request_headers_to_add())),
      redirect_url_rewrite_regex_(
          policy_config.has_redirect_url_rewrite_regex()
              ? Regex::Utility::parseRegex(policy_config.redirect_url_rewrite_regex().pattern())
              : nullptr),
      redirect_url_rewrite_regex_substitution_(
          policy_config.has_redirect_url_rewrite_regex()
              ? policy_config.redirect_url_rewrite_regex().substitution()
              : ""),
      host_rewrite_(policy_config.host_rewrite_literal()),
      forced_use_original_host_(policy_config.forced_use_original_host()),
      forced_add_header_before_route_matcher_(policy_config.forced_add_header_before_route_matcher()) {
  for (const auto& predicate : policy_config.predicates()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<InternalRedirectPredicateFactory>(predicate);
    auto config = factory.createEmptyConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(predicate.typed_config(), validator, *config);
    predicate_factories_.emplace_back(&factory, std::move(config));
  }
}

InternalActiveRedirectPolicyImpl::InternalActiveRedirectPolicyImpl(
    const envoy::config::route::v3::InternalActiveRedirectPolicy::RedirectPolicy& policy_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name)
    : current_route_name_(current_route_name),
      redirect_response_codes_(buildRedirectResponseCodes(policy_config)),
      max_internal_redirects_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(policy_config, max_internal_redirects, 1)),
      enabled_(true), allow_cross_scheme_redirect_(policy_config.allow_cross_scheme_redirect()),
      redirect_url_(policy_config.redirect_url()),
      request_headers_parser_(HeaderParser::configure(policy_config.request_headers_to_add())),
      redirect_url_rewrite_regex_(
          policy_config.has_redirect_url_rewrite_regex()
              ? Regex::Utility::parseRegex(policy_config.redirect_url_rewrite_regex().pattern())
              : nullptr),
      redirect_url_rewrite_regex_substitution_(
          policy_config.has_redirect_url_rewrite_regex()
              ? policy_config.redirect_url_rewrite_regex().substitution()
              : ""),
      host_rewrite_(policy_config.host_rewrite_literal()) {
  for (const auto& predicate : policy_config.predicates()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<InternalRedirectPredicateFactory>(predicate);
    auto config = factory.createEmptyConfigProto();
    Envoy::Config::Utility::translateOpaqueConfig(predicate.typed_config(), validator, *config);
    predicate_factories_.emplace_back(&factory, std::move(config));
  }
}

std::vector<InternalRedirectPredicateSharedPtr>
InternalActiveRedirectPolicyImpl::predicates() const {
  std::vector<InternalRedirectPredicateSharedPtr> predicates;
  for (const auto& predicate_factory : predicate_factories_) {
    predicates.emplace_back(predicate_factory.first->createInternalRedirectPredicate(
        *predicate_factory.second, current_route_name_));
  }
  return predicates;
}

absl::flat_hash_set<Http::Code> InternalActiveRedirectPolicyImpl::buildRedirectResponseCodes(
    const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config) const {
  if (policy_config.redirect_response_codes_size() == 0) {
    return absl::flat_hash_set<Http::Code>{};
  }

  absl::flat_hash_set<Http::Code> ret;
  std::for_each(policy_config.redirect_response_codes().begin(),
                policy_config.redirect_response_codes().end(), [&ret](uint32_t response_code) {
                  const absl::flat_hash_set<uint32_t> valid_redirect_response_code = {
                      301, 302, 303, 307, 308, 200};
                  if (!valid_redirect_response_code.contains(response_code)) {
                    ret.insert(static_cast<Http::Code>(response_code));
                  }
                });
  return ret;
}

absl::flat_hash_set<Http::Code> InternalActiveRedirectPolicyImpl::buildRedirectResponseCodes(
    const envoy::config::route::v3::InternalActiveRedirectPolicy::RedirectPolicy& policy_config)
    const {
  if (policy_config.redirect_response_codes_size() == 0) {
    return absl::flat_hash_set<Http::Code>{};
  }

  absl::flat_hash_set<Http::Code> ret;
  std::for_each(policy_config.redirect_response_codes().begin(),
                policy_config.redirect_response_codes().end(), [&ret](uint32_t response_code) {
                  const absl::flat_hash_set<uint32_t> valid_redirect_response_code = {
                      301, 302, 303, 307, 308, 200};
                  if (!valid_redirect_response_code.contains(response_code)) {
                    ret.insert(static_cast<Http::Code>(response_code));
                  }
                });
  return ret;
}

void InternalActiveRedirectPolicyImpl::evaluateHeaders(
    Http::HeaderMap& headers, const StreamInfo::StreamInfo* stream_info) const {
  request_headers_parser_->evaluateHeaders(headers, stream_info);
  if (!host_rewrite_.empty()) {
    Http::RequestHeaderMap& request_headers = dynamic_cast<Http::RequestHeaderMap&>(headers);
    request_headers.setHost(host_rewrite_);
  }
}

std::string
InternalActiveRedirectPolicyImpl::redirectUrl(absl::optional<std::string> current_path) const {
  if (!redirect_url_.empty()) {
    ENVOY_LOG(debug, "The redirect url: {}", redirect_url_);
    return redirect_url_;
  }

  RELEASE_ASSERT(current_path.has_value(),
                 "The internal redirect address uses a regular expression, but does not pass in "
                 "the current path value");
  auto just_path(Http::PathUtil::removeQueryAndFragment(current_path.value()));
  return redirect_url_rewrite_regex_->replaceAll(just_path,
                                                 redirect_url_rewrite_regex_substitution_);
}

bool InternalActiveRedirectPolicyImpl::forcedUseOriginalHost() const {
  return forced_use_original_host_;
}

InternalActiveRedirectPoliciesImpl::InternalActiveRedirectPoliciesImpl(
    const envoy::config::route::v3::InternalActiveRedirectPolicy& policy_config,
    ProtobufMessage::ValidationVisitor& validator, absl::string_view current_route_name) {
  if (policy_config.policies().empty() && !policy_config.redirect_response_codes().empty()) {
    ENVOY_LOG(warn, "Please configure the redirection policy using the Policies field, the old "
                    "configuration will be deprecated");
    auto policy = std::make_unique<InternalActiveRedirectPolicyImpl>(policy_config, validator,
                                                                     current_route_name);
    policies_.emplace_back(std::move(policy));
  }

  for (const auto& policy : policy_config.policies()) {
    auto policy_impl =
        std::make_unique<InternalActiveRedirectPolicyImpl>(policy, validator, current_route_name);
    policies_.emplace_back(std::move(policy_impl));
  }

  if (policies_.empty()) {
    ENVOY_LOG(warn, "No redirection policy is currently configured. A default value is generated");
    auto policy_impl = std::make_unique<InternalActiveRedirectPolicyImpl>();
    policies_.emplace_back(std::move(policy_impl));
  }
}

InternalActiveRedirectPoliciesImpl::InternalActiveRedirectPoliciesImpl() {
  auto policy_impl = std::make_unique<InternalActiveRedirectPolicyImpl>();
  policies_.emplace_back(std::move(policy_impl));
}

std::vector<InternalRedirectPredicateSharedPtr>
InternalActiveRedirectPoliciesImpl::predicates() const {
  return policies_.at(current_policy_index_)->predicates();
}

void InternalActiveRedirectPoliciesImpl::evaluateHeaders(
    Http::HeaderMap& headers, const StreamInfo::StreamInfo* stream_info) const {
  return policies_.at(current_policy_index_)->evaluateHeaders(headers, stream_info);
}

std::string
InternalActiveRedirectPoliciesImpl::redirectUrl(absl::optional<std::string> current_path) const {
  return policies_.at(current_policy_index_)->redirectUrl(current_path);
}

bool InternalActiveRedirectPoliciesImpl::enabled() const {
  return policies_.at(current_policy_index_)->enabled();
}

bool InternalActiveRedirectPoliciesImpl::shouldRedirectForResponseCode(
    const Http::Code& response_code) const {
  for (ActiveRedirectPolicies::size_type i = 0; i < policies_.size(); i++) {
    if (policies_.at(i)->shouldRedirectForResponseCode(response_code)) {
      current_policy_index_ = i;
      return true;
    }
  }

  return false;
}

uint32_t InternalActiveRedirectPoliciesImpl::maxInternalRedirects() const {
  return policies_.at(current_policy_index_)->maxInternalRedirects();
}

bool InternalActiveRedirectPoliciesImpl::isCrossSchemeRedirectAllowed() const {
  return policies_.at(current_policy_index_)->isCrossSchemeRedirectAllowed();
}

bool InternalActiveRedirectPoliciesImpl::forcedUseOriginalHost() const {
  return policies_.at(current_policy_index_)->forcedUseOriginalHost();
}

bool InternalActiveRedirectPoliciesImpl::forcedAddHeaderBeforeRouteMatcher() const {
  return policies_.at(current_policy_index_)->forcedAddHeaderBeforeRouteMatcher();
}

} // namespace Router
} // namespace Envoy
