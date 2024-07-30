#include "contrib/custom_cluster_plugins/cluster_fallback/source/config.h"

#include "contrib/custom_cluster_plugins/cluster_fallback/source/filter.h"

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

Envoy::Router::ClusterSpecifierPluginSharedPtr
ClusterFallbackPluginFactoryConfig::createClusterSpecifierPlugin(
    const Protobuf::Message& config, Server::Configuration::CommonFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::custom_cluster_plugins::
                                           cluster_fallback::v3::ClusterFallbackConfig&>(
          config, context.messageValidationVisitor());
  return std::make_shared<ClusterFallbackPlugin>(proto_config, context);
}

REGISTER_FACTORY(ClusterFallbackPluginFactoryConfig,
                 Envoy::Router::ClusterSpecifierPluginFactoryConfig);

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
