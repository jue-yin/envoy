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
        "//contrib/http_dubbo_transcoder/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "dubbo_transcoder_filter_test",
    data = [
        "//contrib/http_dubbo_transcoder/filters/http/test/test_data:http2dubbo_test_data"
    ],
    external_deps = [
        "hessian2_codec_object_impl",
        "hessian2_codec_codec_impl",
        "hessian2_codec_object_codec_lib",
    ],
    srcs = ["dubbo_transcoder_filter_test.cc"],
    deps = [
        "//contrib/http_dubbo_transcoder/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)
