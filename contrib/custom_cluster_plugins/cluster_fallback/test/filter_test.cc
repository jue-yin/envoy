#include "contrib/custom_cluster_plugins/cluster_fallback/source/filter.h"

#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.h"
#include "contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3/cluster_fallback.pb.validate.h"
#include "contrib/custom_cluster_plugins/cluster_fallback/source/config.h"
#include "source/common/router/config_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ReturnRef;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace CustomClusterPlugins {
namespace ClusterFallback {

Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method, const std::string& scheme) {
  auto hdrs =
      Http::TestRequestHeaderMapImpl{{":authority", host},         {":path", path},
                                     {":method", method},          {"x-safe", "safe"},
                                     {"x-global-nope", "global"},  {"x-vhost-nope", "vhost"},
                                     {"x-route-nope", "route"},    {":scheme", scheme},
                                     {"x-forwarded-proto", scheme}};

  if (scheme.empty()) {
    hdrs.remove(":scheme");
  }

  return hdrs;
}

Http::TestRequestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return genHeaders(host, path, method, "http");
}

TEST(ClusterFallbackPluginTest, NormalWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: envoy.router.cluster_specifier_plugin.cluster_fallback
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
            cluster_config:
              routing_cluster: test
              fallback_clusters:
              - fallback1
              - fallback2
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  std::shared_ptr<Upstream::MockThreadLocalCluster> test_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{mock_host};
  Envoy::Upstream::MockHostSet* mock_host_set =
      test_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*mock_host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(testing::Return(test_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("test", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, OnceFallbackWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: envoy.router.cluster_specifier_plugin.cluster_fallback
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
            cluster_config:
              routing_cluster: test
              fallback_clusters:
              - fallback1
              - fallback2
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{host};
  Envoy::Upstream::MockHostSet* mock_host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*mock_host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback1", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, TwiceFallbackWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: envoy.router.cluster_specifier_plugin.cluster_fallback
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
            cluster_config:
              routing_cluster: test
              fallback_clusters:
              - fallback1
              - fallback2
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // cluster test does not exist.
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  // cluster fallback1 is empty.
  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::MockHostSet* mock_host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  Envoy::Upstream::HostVector empty_hosts{};
  EXPECT_CALL(*mock_host_set, healthyHosts()).WillOnce(ReturnRef(empty_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback2_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{host};
  Envoy::Upstream::MockHostSet* host_set =
      fallback2_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback2")))
      .WillOnce(Return(fallback2_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback2", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, NoHealthClusterWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/bar"
    route:
      inline_cluster_specifier_plugin:
        extension:
          name: envoy.router.cluster_specifier_plugin.cluster_fallback
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
            cluster_config:
              routing_cluster: test
              fallback_clusters:
              - fallback1
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // cluster test is empty.
  std::shared_ptr<Upstream::MockThreadLocalCluster> test_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::MockHostSet* mock_host_set_test =
      test_cluster->cluster_.prioritySet().getMockHostSet(0);
  Envoy::Upstream::HostVector empty_hosts_test{};
  EXPECT_CALL(*mock_host_set_test, healthyHosts()).WillOnce(ReturnRef(empty_hosts_test));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(testing::Return(test_cluster.get()));

  // cluster fallback1 is empty.
  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::MockHostSet* mock_host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  Envoy::Upstream::HostVector empty_hosts{};
  EXPECT_CALL(*mock_host_set, healthyHosts()).WillOnce(ReturnRef(empty_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto route = config.route(genHeaders("some_cluster", "/bar", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("test", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, ClusterSpecifierPlugin) {
  const std::string yaml = R"EOF(
cluster_specifier_plugins:
- extension:
    name: envoy.router.cluster_specifier_plugin.cluster_fallback
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
      cluster_config:
        routing_cluster: test
        fallback_clusters:
        - fallback1
        - fallback2
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  - match:
      prefix: "/bar"
    route:
      cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // cluster test is empty.
  std::shared_ptr<Upstream::MockThreadLocalCluster> test_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::MockHostSet* mock_host_set_test =
      test_cluster->cluster_.prioritySet().getMockHostSet(0);
  Envoy::Upstream::HostVector empty_hosts_test{};
  EXPECT_CALL(*mock_host_set_test, healthyHosts()).Times(2).WillRepeatedly(ReturnRef(empty_hosts_test));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .Times(2).WillRepeatedly(Return(test_cluster.get()));

  // cluster fallback1 is empty.
  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::MockHostSet* mock_host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  Envoy::Upstream::HostVector empty_hosts{};
  EXPECT_CALL(*mock_host_set, healthyHosts()).Times(2).WillRepeatedly(ReturnRef(empty_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .Times(2).WillRepeatedly(Return(fallback1_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback2_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{host};
  Envoy::Upstream::MockHostSet* host_set =
      fallback2_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).Times(2).WillRepeatedly(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback2")))
      .Times(2).WillRepeatedly(Return(fallback2_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto route = config.route(genHeaders("some_cluster", "/bar", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback2", route->routeEntry()->clusterName());

  route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback2", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, WeightedClusterNormalWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - name: test
          weight: 100
        - name: cluster2
          weight: 0
        inline_cluster_specifier_plugin:
          extension:
            name: envoy.router.cluster_specifier_plugin.cluster_fallback
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
              weighted_cluster_config:
                config:
                - routing_cluster: test
                  fallback_clusters:
                  - fallback1
                - routing_cluster: cluster2
                  fallback_clusters:
                  - fallback2
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  std::shared_ptr<Upstream::MockThreadLocalCluster> test_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{mock_host};
  Envoy::Upstream::MockHostSet* mock_host_set =
      test_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*mock_host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(testing::Return(test_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("test", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, WeightedClusterFallbackWithInlinePlugin) {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - name: test
          weight: 100
        - name: cluster2
          weight: 0
        inline_cluster_specifier_plugin:
          extension:
            name: envoy.router.cluster_specifier_plugin.cluster_fallback
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
              weighted_cluster_config:
                config:
                - routing_cluster: test
                  fallback_clusters:
                  - fallback1
                - routing_cluster: cluster2
                  fallback_clusters:
                  - fallback2
  - match:
      prefix: "/bar"
    route:
      cluster_header: some_header
      timeout: 0s
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // cluster test does not exist.
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{mock_host};
  Envoy::Upstream::MockHostSet* host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback1", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, WeightedClusterFallback) {
  const std::string yaml = R"EOF(
cluster_specifier_plugins:
- extension:
    name: envoy.router.cluster_specifier_plugin.cluster_fallback
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
      weighted_cluster_config:
        config:
        - routing_cluster: test
          fallback_clusters:
          - fallback1
        - routing_cluster: cluster2
          fallback_clusters:
          - fallback2
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - name: test
          weight: 100
        - name: cluster2
          weight: 0
        cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  - match:
      prefix: "/bar"
    route:
      cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // cluster test does not exist.
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{mock_host};
  Envoy::Upstream::MockHostSet* host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback1", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, WeightedClusterNoHealthHost) {
  const std::string yaml = R"EOF(
cluster_specifier_plugins:
- extension:
    name: envoy.router.cluster_specifier_plugin.cluster_fallback
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
      weighted_cluster_config:
        config:
        - routing_cluster: test
          fallback_clusters:
          - fallback1
        - routing_cluster: cluster2
          fallback_clusters:
          - fallback2
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - name: test
          weight: 100
        - name: cluster2
          weight: 0
        cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  - match:
      prefix: "/bar"
    route:
      cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // cluster test does not exist.
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  Envoy::Upstream::HostVector mock_hosts;
  Envoy::Upstream::MockHostSet* host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  auto route = config.route(genHeaders("some_cluster", "/foo", "GET"), stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("test", route->routeEntry()->clusterName());
}

TEST(ClusterFallbackPluginTest, WeightedClusterFallbackViaClusterHeader) {
  const std::string yaml = R"EOF(
cluster_specifier_plugins:
- extension:
    name: envoy.router.cluster_specifier_plugin.cluster_fallback
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.custom_cluster_plugins.cluster_fallback.v3.ClusterFallbackConfig
      weighted_cluster_config:
        config:
        - routing_cluster: test
          fallback_clusters:
          - fallback1
        - routing_cluster: cluster2
          fallback_clusters:
          - fallback2
virtual_hosts:
- name: local_service
  domains:
  - "*"
  routes:
  - match:
      prefix: "/foo"
    route:
      weighted_clusters:
        clusters:
        - cluster_header: cluster
          weight: 100
        - name: cluster2
          weight: 0
        cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  - match:
      prefix: "/bar"
    route:
      cluster_specifier_plugin: envoy.router.cluster_specifier_plugin.cluster_fallback
  )EOF";

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // cluster test does not exist.
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("test")))
      .WillOnce(Return(nullptr));

  std::shared_ptr<Upstream::MockThreadLocalCluster> fallback1_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  auto mock_host = std::make_shared<NiceMock<Envoy::Upstream::MockHost>>();
  Envoy::Upstream::HostVector mock_hosts{mock_host};
  Envoy::Upstream::MockHostSet* host_set =
      fallback1_cluster->cluster_.prioritySet().getMockHostSet(0);
  EXPECT_CALL(*host_set, healthyHosts()).WillOnce(ReturnRef(mock_hosts));
  EXPECT_CALL(factory_context.cluster_manager_, getThreadLocalCluster(testing::Eq("fallback1")))
      .WillOnce(Return(fallback1_cluster.get()));

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);

  const Envoy::Router::OptionalHttpFilters& optional_http_filters =
      Envoy::Router::OptionalHttpFilters();
  Envoy::Router::ConfigImpl config(route_config, optional_http_filters, factory_context,
                                   ProtobufMessage::getNullValidationVisitor(), false);

  Http::TestRequestHeaderMapImpl header = genHeaders("some_cluster", "/foo", "GET");
  header.setByKey("cluster", "test");
  auto route = config.route(header, stream_info, 0);
  EXPECT_NE(nullptr, route);
  EXPECT_EQ("fallback1", route->routeEntry()->clusterName());
}

} // namespace ClusterFallback
} // namespace CustomClusterPlugins
} // namespace Extensions
} // namespace Envoy
