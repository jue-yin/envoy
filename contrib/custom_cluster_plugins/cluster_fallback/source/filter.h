#pragma once

#include <string>
#include <vector>

#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.h"
#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.validate.h"

#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

class ClusterFallbackPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                              public Logger::Loggable<Logger::Id::router> {
public:
  ClusterFallbackPlugin(
      const envoy::extensions::custom_cluster_plugins::cluster_fallback::v3::ClusterFallbackConfig&
          config,
      Server::Configuration::CommonFactoryContext& context);

  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteConstSharedPtr route,
                                           const Http::RequestHeaderMap&) const;

private:
  bool hasHealthHost(absl::string_view cluster_name) const;
  Envoy::Router::RouteConstSharedPtr
  calculateWeightedClusterFallback(const Envoy::Router::RouteEntry& route_entry) const;
  Envoy::Router::RouteConstSharedPtr
  calculateNormalClusterFallback(const Envoy::Router::RouteEntry& route_entry) const;

  Upstream::ClusterManager& cluster_manager_;
  std::unordered_map<std::string/*routing cluster*/, std::vector<std::string>/*fallback clusters*/> clusters_config_;
};

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
