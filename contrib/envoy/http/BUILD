load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "active_redirect_policy_interface",
    hdrs = ["active_redirect_policy.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//envoy/router:internal_redirect_interface",
        "//envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
