#include "source/common/router/scoped_config_impl.h"

#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#if defined(HIGRESS)
#include "source/common/http/header_utility.h"
#endif

namespace Envoy {
namespace Router {

#if defined(HIGRESS)
namespace {

std::string maskFirstDNSLabel(absl::string_view host) {
  if (host == "*") {
    return std::string(host);
  }
  if (host.size() < 2) {
    return "*";
  }
  size_t start_pos = (host[0] == '*' && host[1] == '.') ? 2 : 0;
  size_t dot_pos = host.find('.', start_pos);
  if (dot_pos != absl::string_view::npos) {
    return absl::StrCat("*", host.substr(dot_pos));
  }
  return "*";
}

} // namespace

LocalPortValueExtractorImpl::LocalPortValueExtractorImpl(
    ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
    : FragmentBuilderBase(std::move(config)) {
  ASSERT(config_.type_case() ==
             ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kLocalPortValueExtractor,
         "local_port_value_extractor is not set.");
}

std::unique_ptr<ScopeKeyFragmentBase> LocalPortValueExtractorImpl::computeFragment(
    const Http::HeaderMap&, const StreamInfo::StreamInfo* info, ReComputeCbPtr&) const {
  ASSERT(info != nullptr, "streamInfo is nullptr.");
  auto port = info->downstreamAddressProvider().directLocalAddress()->ip()->port();
  return std::make_unique<StringKeyFragment>(std::to_string(long(port)));
}

HostValueExtractorImpl::HostValueExtractorImpl(
    ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
    : FragmentBuilderBase(std::move(config)),
      host_value_extractor_config_(config_.host_value_extractor()),
      max_recompute_num_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          host_value_extractor_config_, max_recompute_num, DefaultMaxRecomputeNum)) {
  ASSERT(config_.type_case() == ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHostValueExtractor,
         "host_value_extractor is not set.");
}

std::unique_ptr<ScopeKeyFragmentBase>
HostValueExtractorImpl::reComputeHelper(const std::string& host,
                                        ReComputeCbWeakPtr& weak_next_recompute,
                                        uint32_t recompute_seq) const {
  if (recompute_seq == max_recompute_num_) {
    ENVOY_LOG_MISC(warn,
                   "recompute host fragment failed, maximum number of recalculations exceeded");
    return nullptr;
  }
  auto next_recompute = weak_next_recompute.lock();
  if (!next_recompute) {
    return nullptr;
  }
  if (host == "*") {
    *next_recompute = nullptr;
    return nullptr;
  }
  auto masked_host = maskFirstDNSLabel(host);
  *next_recompute = [this, masked_host, recompute_seq,
                     weak_next_recompute]() mutable -> std::unique_ptr<ScopeKeyFragmentBase> {
    return reComputeHelper(masked_host, weak_next_recompute, recompute_seq + 1);
  };
  return std::make_unique<StringKeyFragment>(masked_host);
}

std::unique_ptr<ScopeKeyFragmentBase>
HostValueExtractorImpl::computeFragment(const Http::HeaderMap& headers,
                                        const StreamInfo::StreamInfo*,
                                        ReComputeCbPtr& recompute) const {
  auto host = static_cast<const Http::RequestHeaderMap&>(headers).getHostValue();
  auto port_start = Http::HeaderUtility::getPortStart(host);
  if (port_start != absl::string_view::npos) {
    host = host.substr(0, port_start);
  }
  *recompute = [this, host_str = std::string(host),
                weak_recompute = ReComputeCbWeakPtr(
                    recompute)]() mutable -> std::unique_ptr<ScopeKeyFragmentBase> {
    return reComputeHelper(host_str, weak_recompute, 0);
  };
  return std::make_unique<StringKeyFragment>(host);
}

std::unique_ptr<ScopeKeyFragmentBase>
HeaderValueExtractorImpl::computeFragment(const Http::HeaderMap& headers,
                                          const StreamInfo::StreamInfo*, ReComputeCbPtr&) const {
  return computeFragment(headers);
}

ScopeKeyPtr ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers,
                                                 const StreamInfo::StreamInfo* info,
                                                 std::function<ScopeKeyPtr()>& recompute) const {
  ScopeKey key;
  bool recomputeable = false;
  auto recompute_cbs = std::make_shared<std::vector<ReComputeCbPtr>>();
  for (const auto& builder : fragment_builders_) {
    // returns nullopt if a null fragment is found.
    ReComputeCbPtr recompute_fragment_cb = std::make_shared<ReComputeCb>();
    std::unique_ptr<ScopeKeyFragmentBase> fragment =
        builder->computeFragment(headers, info, recompute_fragment_cb);
    if (fragment == nullptr) {
      return nullptr;
    }
    if (*recompute_fragment_cb == nullptr) {
      auto key_fragment = static_cast<StringKeyFragment*>(fragment.get());
      auto copied_fragment = std::make_shared<StringKeyFragment>(*key_fragment);
      auto recompute_cb =
          std::make_shared<ReComputeCb>([copied_fragment]() -> std::unique_ptr<StringKeyFragment> {
            return std::make_unique<StringKeyFragment>(*copied_fragment);
          });
      recompute_cbs->push_back(recompute_cb);
    } else {
      recomputeable = true;
      recompute_cbs->push_back(recompute_fragment_cb);
    }
    key.addFragment(std::move(fragment));
  }
  if (recomputeable) {
    recompute = [&recompute, recompute_cbs]() mutable -> ScopeKeyPtr {
      ScopeKey new_key;
      for (auto& cb : *recompute_cbs) {
        if (*cb == nullptr) {
          recompute = nullptr;
          return nullptr;
        }
        auto new_fragment = (*cb)();
        if (new_fragment == nullptr) {
          return nullptr;
        }
        new_key.addFragment(std::move(new_fragment));
      }
      return std::make_unique<ScopeKey>(std::move(new_key));
    };
  }
  return std::make_unique<ScopeKey>(std::move(key));
}

ScopeKeyPtr ScopedConfigImpl::computeScopeKey(const ScopeKeyBuilder* scope_key_builder,
                                              const Http::HeaderMap& headers,
                                              const StreamInfo::StreamInfo* info) const {
  std::function<Router::ScopeKeyPtr()> recompute;
  ScopeKeyPtr scope_key = scope_key_builder->computeScopeKey(headers, info, recompute);
  if (scope_key == nullptr) {
    return nullptr;
  }
  decltype(scoped_route_info_by_key_.begin()) iter;
  do {
    iter = scoped_route_info_by_key_.find(scope_key->hash());
    if (iter != scoped_route_info_by_key_.end()) {
      return scope_key;
    }
  } while (recompute != nullptr && (scope_key = recompute()));
  return nullptr;
}

Router::ConfigConstSharedPtr
ScopedConfigImpl::getRouteConfig(const ScopeKeyBuilder* scope_key_builder,
                                 const Http::HeaderMap& headers, const StreamInfo::StreamInfo* info,
                                 std::function<ScopeKeyPtr()>& recompute) const {
  ScopeKeyPtr scope_key = nullptr;
  if (recompute == nullptr) {
    scope_key = scope_key_builder->computeScopeKey(headers, info, recompute);
  } else {
    scope_key = recompute();
  }
  if (scope_key == nullptr) {
    return nullptr;
  }
  decltype(scoped_route_info_by_key_.begin()) iter;
  do {
    iter = scoped_route_info_by_key_.find(scope_key->hash());
    if (iter != scoped_route_info_by_key_.end()) {
      return iter->second->routeConfig();
    }
  } while (recompute != nullptr && (scope_key = recompute()));

  return nullptr;
}

Router::ConfigConstSharedPtr
ScopedConfigImpl::getRouteConfig(const ScopeKeyBuilder* scope_key_builder,
                                 const Http::HeaderMap& headers,
                                 const StreamInfo::StreamInfo* info) const {
  std::function<Router::ScopeKeyPtr()> recompute;
  return getRouteConfig(scope_key_builder, headers, info, recompute);
}

#endif

bool ScopeKey::operator!=(const ScopeKey& other) const { return !(*this == other); }

bool ScopeKey::operator==(const ScopeKey& other) const {
  if (fragments_.empty() || other.fragments_.empty()) {
    // An empty key equals to nothing, "NULL" != "NULL".
    return false;
  }
  return this->hash() == other.hash();
}

HeaderValueExtractorImpl::HeaderValueExtractorImpl(
    ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
    : FragmentBuilderBase(std::move(config)),
      header_value_extractor_config_(config_.header_value_extractor()) {
  ASSERT(config_.type_case() ==
             ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor,
         "header_value_extractor is not set.");
  if (header_value_extractor_config_.extract_type_case() ==
      ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex) {
    if (header_value_extractor_config_.index() != 0 &&
        header_value_extractor_config_.element_separator().empty()) {
      throw ProtoValidationException("Index > 0 for empty string element separator.",
                                     header_value_extractor_config_);
    }
  }
  if (header_value_extractor_config_.extract_type_case() ==
      ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::EXTRACT_TYPE_NOT_SET) {
    throw ProtoValidationException("HeaderValueExtractor extract_type not set.",
                                   header_value_extractor_config_);
  }
}

std::unique_ptr<ScopeKeyFragmentBase>
HeaderValueExtractorImpl::computeFragment(const Http::HeaderMap& headers) const {
  const auto header_entry =
      headers.get(Envoy::Http::LowerCaseString(header_value_extractor_config_.name()));
  if (header_entry.empty()) {
    return nullptr;
  }

  // This is an implicitly untrusted header, so per the API documentation only the first
  // value is used.
  std::vector<absl::string_view> elements{header_entry[0]->value().getStringView()};
  if (header_value_extractor_config_.element_separator().length() > 0) {
    elements = absl::StrSplit(header_entry[0]->value().getStringView(),
                              header_value_extractor_config_.element_separator());
  }
  switch (header_value_extractor_config_.extract_type_case()) {
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kElement:
    for (const auto& element : elements) {
      std::pair<absl::string_view, absl::string_view> key_value = absl::StrSplit(
          element, absl::MaxSplits(header_value_extractor_config_.element().separator(), 1));
      if (key_value.first == header_value_extractor_config_.element().key()) {
        return std::make_unique<StringKeyFragment>(key_value.second);
      }
    }
    break;
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex:
    if (header_value_extractor_config_.index() < elements.size()) {
      return std::make_unique<StringKeyFragment>(elements[header_value_extractor_config_.index()]);
    }
    break;
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::EXTRACT_TYPE_NOT_SET:
    PANIC("not reached");
  }

  return nullptr;
}

ScopedRouteInfo::ScopedRouteInfo(envoy::config::route::v3::ScopedRouteConfiguration config_proto,
                                 ConfigConstSharedPtr route_config)
    : config_proto_(config_proto), route_config_(route_config),
      config_hash_(MessageUtil::hash(config_proto)) {
  // TODO(stevenzzzz): Maybe worth a KeyBuilder abstraction when there are more than one type of
  // Fragment.
  for (const auto& fragment : config_proto_.key().fragments()) {
    switch (fragment.type_case()) {
    case envoy::config::route::v3::ScopedRouteConfiguration::Key::Fragment::TypeCase::kStringKey:
      scope_key_.addFragment(std::make_unique<StringKeyFragment>(fragment.string_key()));
      break;
    case envoy::config::route::v3::ScopedRouteConfiguration::Key::Fragment::TypeCase::TYPE_NOT_SET:
      PANIC("not implemented");
    }
  }
}

ScopeKeyBuilderImpl::ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config)
    : ScopeKeyBuilderBase(std::move(config)) {
  for (const auto& fragment_builder : config_.fragments()) {
    switch (fragment_builder.type_case()) {
#if defined(HIGRESS)
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHostValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<HostValueExtractorImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kLocalPortValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<LocalPortValueExtractorImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
#endif
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<HeaderValueExtractorImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::TYPE_NOT_SET:
      PANIC("not implemented");
    }
  }
}

ScopeKeyPtr ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers) const {
  ScopeKey key;
  for (const auto& builder : fragment_builders_) {
    // returns nullopt if a null fragment is found.
#if defined(HIGRESS)
    ReComputeCbPtr recompute_fragment_cb = std::make_shared<ReComputeCb>();
    std::unique_ptr<ScopeKeyFragmentBase> fragment =
        builder->computeFragment(headers, nullptr, recompute_fragment_cb);
#else
    std::unique_ptr<ScopeKeyFragmentBase> fragment = builder->computeFragment(headers);
#endif
    if (fragment == nullptr) {
      return nullptr;
    }
    key.addFragment(std::move(fragment));
  }
  return std::make_unique<ScopeKey>(std::move(key));
}

void ScopedConfigImpl::addOrUpdateRoutingScopes(
    const std::vector<ScopedRouteInfoConstSharedPtr>& scoped_route_infos) {
  for (auto& scoped_route_info : scoped_route_infos) {
    const auto iter = scoped_route_info_by_name_.find(scoped_route_info->scopeName());
    if (iter != scoped_route_info_by_name_.end()) {
      ASSERT(scoped_route_info_by_key_.contains(iter->second->scopeKey().hash()));
      scoped_route_info_by_key_.erase(iter->second->scopeKey().hash());
    }
    scoped_route_info_by_name_[scoped_route_info->scopeName()] = scoped_route_info;
    scoped_route_info_by_key_[scoped_route_info->scopeKey().hash()] = scoped_route_info;
  }
}

void ScopedConfigImpl::removeRoutingScopes(const std::vector<std::string>& scope_names) {
  for (std::string const& scope_name : scope_names) {
    const auto iter = scoped_route_info_by_name_.find(scope_name);
    if (iter != scoped_route_info_by_name_.end()) {
      ASSERT(scoped_route_info_by_key_.contains(iter->second->scopeKey().hash()));
      scoped_route_info_by_key_.erase(iter->second->scopeKey().hash());
      scoped_route_info_by_name_.erase(iter);
    }
  }
}

Router::ConfigConstSharedPtr ScopedConfigImpl::getRouteConfig(const ScopeKeyPtr& scope_key) const {
  if (scope_key == nullptr) {
    return nullptr;
  }
  auto iter = scoped_route_info_by_key_.find(scope_key->hash());
  if (iter != scoped_route_info_by_key_.end()) {
    return iter->second->routeConfig();
  }
  return nullptr;
}

} // namespace Router
} // namespace Envoy
