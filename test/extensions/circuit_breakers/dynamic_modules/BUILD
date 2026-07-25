load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:circuit_breaker_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:circuit_breaker_no_op",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/upstream:resource_manager_factory_interface",
        "//source/extensions/circuit_breakers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/circuit_breakers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:circuit_breaker_per_tenant_integration_test",
    ],
    deps = [
        "//source/extensions/circuit_breakers/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@abseil-cpp//absl/strings",
    ],
)
