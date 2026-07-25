load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Circuit breaker that delegates admission decisions for a cluster resource dimension to a dynamic
# module loaded via dlopen (see source/extensions/dynamic_modules/abi).

envoy_extension_package()

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":resource_limit_lib",
        "//envoy/http:header_map_interface",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/strings:string_view",
    ],
    alwayslink = True,
)

envoy_cc_library(
    name = "cb_config_lib",
    srcs = ["cb_config.cc"],
    hdrs = ["cb_config.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/circuit_breakers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_limit_lib",
    hdrs = ["resource_limit.h"],
    deps = [
        ":cb_config_lib",
        "//envoy/common:resource_interface",
        "//source/extensions/dynamic_modules/abi",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":cb_config_lib",
        ":resource_limit_lib",
        "//envoy/registry",
        "//envoy/upstream:resource_manager_factory_interface",
        "//source/common/common:logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/circuit_breakers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
