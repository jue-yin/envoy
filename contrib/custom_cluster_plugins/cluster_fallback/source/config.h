#pragma once

#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.h"
#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.validate.h"

#include "envoy/router/cluster_specifier_plugin.h"

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

class ClusterFallbackPluginFactoryConfig
    : public Envoy::Router::ClusterSpecifierPluginFactoryConfig {
public:
  ClusterFallbackPluginFactoryConfig() = default;

  std::string name() const override {
    return "envoy.router.cluster_specifier_plugin.cluster_fallback";
  }

  Envoy::Router::ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::CommonFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::custom_cluster_plugins::cluster_fallback::v3::ClusterFallbackConfig>();
  }
};

DECLARE_FACTORY(ClusterFallbackPluginFactoryConfig);

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
