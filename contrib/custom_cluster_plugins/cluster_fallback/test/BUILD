load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/custom_cluster_plugins/cluster_fallback/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/custom_cluster_plugins/cluster_fallback/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//contrib/custom_cluster_plugins/cluster_fallback/source:config",
        "//contrib/custom_cluster_plugins/cluster_fallback/source:filter_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)