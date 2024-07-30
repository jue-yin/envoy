load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "dubbo_transcoder_filter_lib",
    srcs = ["dubbo_transcoder_filter.cc"],
    hdrs = ["dubbo_transcoder_filter.h"],
    external_deps = [
        "path_matcher",
        "hessian2_codec_codec_impl",
        "hessian2_codec_object_codec_lib",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":transcoder_interface",
        ":utility_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:query_params_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:regex_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/filters/http:well_known_names",
        "@com_google_googleapis//google/api:http_cc_proto",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":dubbo_transcoder_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "transcoder_interface",
    hdrs = ["transcoder.h"],
    deps = [
        "//envoy/http:filter_interface",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = [
        "hessian2_codec_object_impl",
        "hessian2_codec_codec_impl",
        "hessian2_codec_object_codec_lib",
        "json",
    ],
    deps = [
        ":transcoder_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:query_params_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:regex_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/http_dubbo_transcoder/v3:pkg_cc_proto",
    ],
)
