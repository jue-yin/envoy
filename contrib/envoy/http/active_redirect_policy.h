#pragma once

#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {

namespace Router {

/**
 * InternalActiveRedirectPolicy from the route configuration.
 */
class InternalActiveRedirectPolicy {
public:
  virtual ~InternalActiveRedirectPolicy() = default;

  /**
   * @return whether internal redirect is enabled on this route.
   */
  virtual bool enabled() const PURE;

  /**
   * @param response_code the response code from the upstream.
   * @return whether the given response_code should trigger an internal redirect on this route.
   */
  virtual bool shouldRedirectForResponseCode(const Http::Code& response_code) const PURE;

  /**
   * Creates the target route predicates. This should really be called only once for each upstream
   * redirect response. Creating the predicates lazily to avoid wasting CPU cycles on non-redirect
   * responses, which should be the most common case.
   * @return a vector of newly constructed InternalRedirectPredicate instances.
   */
  virtual std::vector<InternalRedirectPredicateSharedPtr> predicates() const PURE;

  /**
   * @return the maximum number of allowed internal redirects on this route.
   */
  virtual uint32_t maxInternalRedirects() const PURE;

  /**
   * @return if it is allowed to follow the redirect with a different scheme in
   *         the target URI than the downstream request.
   */
  virtual bool isCrossSchemeRedirectAllowed() const PURE;

  virtual void evaluateHeaders(Http::HeaderMap& headers,
                               const StreamInfo::StreamInfo* stream_info) const PURE;

  virtual std::string
  redirectUrl(absl::optional<std::string> current_path = absl::nullopt) const PURE;

  virtual bool forcedUseOriginalHost() const PURE;

  virtual bool forcedAddHeaderBeforeRouteMatcher() const PURE;
};

} // namespace Router
} // namespace Envoy
