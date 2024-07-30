load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client_impl.cc"],
    hdrs = ["async_client_impl.h"],
    deps = [
        "//envoy/stats:stats_macros",
        "//envoy/redis:async_client_interface",

        "//source/common/network:address_lib",
        "//source/common/upstream:upstream_lib",
        "//source/common/upstream:load_balancer_lib",

        "//source/extensions/common/redis:cluster_refresh_manager_lib",
        "//source/extensions/filters/network/common/redis:raw_client_lib",
    ],
)
