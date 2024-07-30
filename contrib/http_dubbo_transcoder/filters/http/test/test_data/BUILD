load(
    "//bazel:envoy_build_system.bzl",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

filegroup(
    name = "http2dubbo_test_data",
    srcs = glob(["big_reqeust_body"]),
)
