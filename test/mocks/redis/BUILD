load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "redis_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/redis:async_client_interface",
        "//envoy/http:filter_interface",
        "//source/common/http:conn_manager_config_interface",
        "//source/common/http:filter_manager_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)
