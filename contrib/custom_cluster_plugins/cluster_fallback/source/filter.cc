#include "contrib/custom_cluster_plugins/cluster_fallback/source/filter.h"

#include "source/common/common/assert.h"

#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

ClusterFallbackPlugin::ClusterFallbackPlugin(
    const envoy::extensions::custom_cluster_plugins::cluster_fallback::v3::ClusterFallbackConfig&
        config,
    Server::Configuration::CommonFactoryContext& context)
    : cluster_manager_(context.clusterManager()) {
  if (config.config_specifier_case() ==
      envoy::extensions::custom_cluster_plugins::cluster_fallback::v3::ClusterFallbackConfig::
          kWeightedClusterConfig) {
    for (auto& item : config.weighted_cluster_config().config()) {
      clusters_config_.emplace(item.routing_cluster(),
                               std::vector<std::string>(item.fallback_clusters().begin(),
                                                        item.fallback_clusters().end()));
    }
  } else {
    clusters_config_.emplace(
        config.cluster_config().routing_cluster(),
        std::vector<std::string>(config.cluster_config().fallback_clusters().begin(),
                                 config.cluster_config().fallback_clusters().end()));
  }

  if (clusters_config_.empty()) {
    ENVOY_LOG(info, "there is no fallback cluster");
  }
}

Envoy::Router::RouteConstSharedPtr
ClusterFallbackPlugin::route(Envoy::Router::RouteConstSharedPtr route,
                             const Http::RequestHeaderMap&) const {
  if (route->routeEntry() != nullptr) {
    auto route_entry = route->routeEntry();
    if (typeid(*route_entry) == typeid(Envoy::Router::RouteEntryImplBase::WeightedClusterEntry&) ||
        typeid(*route_entry) == typeid(Envoy::Router::RouteEntryImplBase::DynamicRouteEntry&)) {
      return calculateWeightedClusterFallback(*route_entry);
    }

    ASSERT(dynamic_cast<const Envoy::Router::RouteEntryImplBase*>(route_entry) != nullptr);
    return calculateNormalClusterFallback(*route_entry);
  }
  PANIC("reached unexpected code");
}

Envoy::Router::RouteConstSharedPtr ClusterFallbackPlugin::calculateNormalClusterFallback(
    const Envoy::Router::RouteEntry& route_entry) const {
  ASSERT(clusters_config_.size() == 1);

  const auto& base = dynamic_cast<const Envoy::Router::RouteEntryImplBase&>(route_entry);
  auto first_item = clusters_config_.begin();
  if (hasHealthHost(first_item->first)) {
    ENVOY_LOG(info, "The target cluster {} has healthy nodes and does not require fallback",
              first_item->first);
    return base.clone(first_item->first);
  }

  for (const auto& cluster_name : first_item->second) {
    if (hasHealthHost(cluster_name)) {
      return base.clone(cluster_name);
    }
  }

  ENVOY_LOG(info, "All clusters have no healthy nodes, the original routing cluster is returned");
  return base.clone(first_item->first);
}

Envoy::Router::RouteConstSharedPtr ClusterFallbackPlugin::calculateWeightedClusterFallback(
    const Envoy::Router::RouteEntry& route_entry) const {
  const auto& cluster_entry =
      dynamic_cast<const Envoy::Router::RouteEntryImplBase::DynamicRouteEntry&>(route_entry);

  auto search = clusters_config_.find(route_entry.clusterName());
  if (search == clusters_config_.end()) {
    ENVOY_LOG(warn, "there is no fallback cluster config, the original routing cluster is returned");
    return cluster_entry.getRouteConstSharedPtr();
  }

  if (hasHealthHost(search->first)) {
    ENVOY_LOG(info, "The target cluster {} has healthy nodes and does not require fallback",
              search->first);
    return cluster_entry.getRouteConstSharedPtr();
  }

  for (const auto& cluster_name : search->second) {
    if (hasHealthHost(cluster_name)) {
      return cluster_entry.clone(cluster_name);
    }
  }

  ENVOY_LOG(info, "All clusters have no healthy nodes, the original routing cluster is returned");
  return cluster_entry.getRouteConstSharedPtr();
}

bool ClusterFallbackPlugin::hasHealthHost(absl::string_view cluster_name) const {
  bool has_health_host{false};
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return has_health_host;
  }

  for (auto& i : cluster->prioritySet().hostSetsPerPriority()) {
    if (i->healthyHosts().size() > 0) {
      has_health_host = true;
      break;
    }
  }

  return has_health_host;
}

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
