#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/custom_cluster_plugins/cluster_fallback/source/config.h"
#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.h"
#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.validate.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

TEST(ClusterFallbackPluginFactoryConfigTest, ClusterFallbackPluginCorrectYaml) {
  const std::string yaml_string = R"EOF(
extension:
  name: envoy.router.cluster_specifier_plugin.cluster_fallback
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
    cluster_config:
      routing_cluster: test
      fallback_clusters:
      - fallback1
      - fallback2
)EOF";

  envoy::config::route::v3::ClusterSpecifierPlugin plugin_config;
  TestUtility::loadFromYaml(yaml_string, plugin_config);

  auto* factory =
      Envoy::Config::Utility::getFactory<Envoy::Router::ClusterSpecifierPluginFactoryConfig>(
          plugin_config.extension());
  EXPECT_NE(nullptr, factory);

  auto config = Envoy::Config::Utility::translateToFactoryConfig(
      plugin_config.extension(), ProtobufMessage::getStrictValidationVisitor(), *factory);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto plugin = factory->createClusterSpecifierPlugin(*config, context);
  EXPECT_NE(nullptr, plugin);
}

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
