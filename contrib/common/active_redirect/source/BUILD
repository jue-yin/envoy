load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "active_redirect_policy_lib",
    srcs = ["active_redirect_policy_impl.cc"],
    hdrs = ["active_redirect_policy_impl.h"],
    external_deps = ["abseil_optional"],
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/envoy/http:active_redirect_policy_interface",
        "//envoy/http:header_map_interface",
        "//envoy/router:router_interface",
        "//source/common/common:empty_string",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:path_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:header_parser_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
