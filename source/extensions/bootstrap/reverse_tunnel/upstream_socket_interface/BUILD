load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Proto library for socket interface configuration
envoy_proto_library(
    name = "reverse_tunnel_acceptor_socket_interface",
    srcs = ["reverse_tunnel_acceptor_socket_interface.proto"],
)

envoy_cc_extension(
    name = "reverse_tunnel_acceptor_includes",
    hdrs = [
        "reverse_connection_io_handle.h",
        "reverse_tunnel_acceptor.h",
        "reverse_tunnel_acceptor_extension.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/network:default_socket_interface_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_reverse_connection_address_lib",
    srcs = ["upstream_reverse_connection_address.cc"],
    hdrs = ["upstream_reverse_connection_address.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:address_interface",
        "//source/common/network:address_lib",
        "//source/common/network:socket_interface_lib",
    ],
)

envoy_cc_extension(
    name = "upstream_reverse_connection_resolver_lib",
    srcs = ["upstream_reverse_connection_resolver.cc"],
    hdrs = ["upstream_reverse_connection_resolver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":upstream_reverse_connection_address_lib",
        "//envoy/network:resolver_interface",
        "//envoy/registry",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "reverse_connection_acceptor_io_handle_lib",
    srcs = ["reverse_connection_acceptor_io_handle.cc"],
    hdrs = ["reverse_connection_acceptor_io_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":accepted_reverse_connection_io_handle_lib",
        "//envoy/network:io_handle_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "accepted_reverse_connection_io_handle_lib",
    srcs = ["accepted_reverse_connection_io_handle.cc"],
    hdrs = ["accepted_reverse_connection_io_handle.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:io_handle_interface",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/common:logger_lib",
    ],
)


envoy_cc_extension(
    name = "reverse_tunnel_acceptor_interface_lib",
    srcs = ["reverse_tunnel_acceptor_interface.cc"],
    hdrs = ["reverse_tunnel_acceptor_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_acceptor_io_handle_lib",
        ":reverse_tunnel_acceptor_socket_interface_cc_proto",
        ":upstream_reverse_connection_address_lib",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//source/common/common:logger_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/protobuf:protobuf",
    ],
)

envoy_cc_extension(
    name = "reverse_tunnel_acceptor_lib",
    srcs = [
        "reverse_connection_io_handle.cc",
        "reverse_tunnel_acceptor.cc",
        "reverse_tunnel_acceptor_extension.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_tunnel_acceptor_includes",
        ":upstream_socket_manager_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/protobuf",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "upstream_socket_manager_lib",
    srcs = ["upstream_socket_manager.cc"],
    hdrs = ["upstream_socket_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        "reverse_tunnel_acceptor_includes",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:random_generator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
    ],
)
