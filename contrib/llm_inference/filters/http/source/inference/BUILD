load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

licenses(["notice"])  # Apache 2

envoy_cc_library(
    name = "inference",
    srcs = [
        "inference_context.cc",
        "inference_task.cc",
        "inference_thread.cc",
    ],
    hdrs = [
        "inference_context.h",
        "inference_task.h",
        "inference_thread.h",
        "utils.hpp",
    ],
    deps = [
        "//source/extensions/filters/http/common:factory_base_lib",
        "@com_google_absl//absl/base",
    ],
    visibility = ["//visibility:public"],
    external_deps = ["llama"],
)