#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_interface.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "envoy/network/address.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "google/protobuf/empty.pb.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_acceptor_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_socket_interface.pb.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseTunnelAcceptorInterface::ReverseTunnelAcceptorInterface() {
  ENVOY_LOG(debug, "Created ReverseTunnelAcceptorInterface");
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptorInterface::socket(Envoy::Network::Socket::Type,
                                      Envoy::Network::Address::Type,
                                      Envoy::Network::Address::IpVersion, bool,
                                      const Envoy::Network::SocketCreationOptions&) const {
  ENVOY_LOG(warn, "ReverseTunnelAcceptorInterface: socket() called without address - returning nullptr.");
  
  // Reverse connection sockets should always have an address.
  return nullptr;
}

Envoy::Network::IoHandlePtr
ReverseTunnelAcceptorInterface::socket(Envoy::Network::Socket::Type socket_type,
                                      const Envoy::Network::Address::InstanceConstSharedPtr addr,
                                      const Envoy::Network::SocketCreationOptions& options) const {

  // Extract upstream reverse connection configuration from address.
  const auto* upstream_addr = dynamic_cast<const UpstreamReverseConnectionAddress*>(addr.get());
  if (upstream_addr) {
    // Get the upstream reverse connection address.
    ENVOY_LOG(debug, "ReverseTunnelAcceptorInterface: upstream_addr: {}", upstream_addr->asString());

    // For stream sockets on IP addresses, create our reverse connection acceptor IOHandle.
    if (socket_type == Envoy::Network::Socket::Type::Stream &&
        addr->type() == Envoy::Network::Address::Type::Ip) {
      
      // Create socket with proper options like in SocketInterfaceImpl
      int protocol = 0;
      int flags = SOCK_NONBLOCK | SOCK_STREAM;
      
      // MPTCP is not relevant for reverse connections, so we skip it
      // if (options.mptcp_enabled_) {
      //   protocol = IPPROTO_MPTCP;
      // }

      int domain;
      Network::Address::IpVersion ip_version = addr->ip() ? addr->ip()->version() : Network::Address::IpVersion::v4;
      if (ip_version == Network::Address::IpVersion::v6 || Network::Address::forceV6()) {
        domain = AF_INET6;
      } else {
        domain = AF_INET;
      }

      const Api::SysCallSocketResult result =
          Api::OsSysCallsSingleton::get().socket(domain, flags, protocol);
      if (!SOCKET_VALID(result.return_value_)) {
        ENVOY_LOG(error, "Failed to create socket for reverse connection: {}", 
                  errorDetails(result.errno_));
        return nullptr;
      }

      int sock_fd = result.return_value_;
      ENVOY_LOG(debug, "ReverseTunnelAcceptorInterface: Created socket fd={}, wrapping with ReverseConnectionAcceptorIOHandle",
                sock_fd);

      // Create our custom ReverseConnectionAcceptorIOHandle
      auto io_handle = std::make_unique<ReverseConnectionAcceptorIOHandle>(sock_fd);

      // Set socket options
      int v6only = 0;
      if (addr->type() == Network::Address::Type::Ip && ip_version == Network::Address::IpVersion::v6) {
        v6only = addr->ip()->ipv6()->v6only();
        const Api::SysCallIntResult v6only_result = io_handle->setOption(
            IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
        if (SOCKET_FAILURE(v6only_result.return_value_)) {
          ENVOY_LOG(error, "Failed to set IPV6_V6ONLY option: {}", v6only_result.return_value_);
          return nullptr;
        }
      }

      // Set SO_REUSEADDR option
      int reuseaddr = 1;
      const Api::SysCallIntResult reuse_result = io_handle->setOption(
          SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseaddr), sizeof(reuseaddr));
      if (SOCKET_FAILURE(reuse_result.return_value_)) {
        ENVOY_LOG(error, "Failed to set SO_REUSEADDR option: {}", reuse_result.return_value_);
        return nullptr;
      }

      // Call bind on our custom IOHandle
      auto bind_result = io_handle->bind(addr);
      if (bind_result.return_value_ != 0) {
        ENVOY_LOG(error, "Failed to bind socket fd={} to address {}: {}",
                  sock_fd, addr->asString(), errorDetails(bind_result.errno_));
        return nullptr;
      }

      ENVOY_LOG(debug, "ReverseTunnelAcceptorInterface: Successfully bound socket fd={} to {}",
                sock_fd, addr->asString());
      
      return io_handle;
    }
  }

  // Fall back to regular socket for non-stream or non-IP sockets.
  return socket(socket_type, addr->type(),
                addr->ip() ? addr->ip()->version() : Envoy::Network::Address::IpVersion::v4, false,
                options);
}

bool ReverseTunnelAcceptorInterface::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

Server::BootstrapExtensionPtr
ReverseTunnelAcceptorInterface::createBootstrapExtension(const Protobuf::Message& /*config*/,
                                                         Server::Configuration::ServerFactoryContext& /*context*/) {
  return std::make_unique<Network::SocketInterfaceExtension>(*this);
}

ProtobufTypes::MessagePtr ReverseTunnelAcceptorInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::ReverseTunnelAcceptorSocketInterface>();
}

// Register the factory
REGISTER_FACTORY(ReverseTunnelAcceptorInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
