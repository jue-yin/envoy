load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "llm_inference_filter_lib",
    srcs = ["llm_inference_filter.cc"],
    hdrs = ["llm_inference_filter.h"],
    deps = [
        "@envoy_api//contrib/envoy/extensions/filters/http/llm_inference/v3:pkg_cc_proto",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//contrib/llm_inference/filters/http/source/inference:inference",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":llm_inference_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/llm_inference/v3:pkg_cc_proto",
    ],
)
