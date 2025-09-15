#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_client_socket_interface.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_client_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

Network::IoHandlePtr ReverseTunnelInitiatorClientSocketInterface::socket(
    Network::Socket::Type socket_type, const Network::Address::InstanceConstSharedPtr addr,
    const Network::SocketCreationOptions& options) const {
  
  ENVOY_LOG(debug, "ReverseTunnelInitiatorClientSocketInterface::socket() called for address: {}", 
            addr ? addr->asString() : "null");

  // Create socket FD using the exact same logic as Network::SocketInterfaceImpl::socket() but wrap
  // the socket in our custom IO handle.
  Network::Address::IpVersion ip_version = addr->ip() ? addr->ip()->version() : Network::Address::IpVersion::v4;
  int v6only = 0;
  if (addr->type() == Network::Address::Type::Ip && ip_version == Network::Address::IpVersion::v6) {
    v6only = addr->ip()->ipv6()->v6only();
  }

  int protocol = 0;
#if defined(__APPLE__) || defined(WIN32)
  ASSERT(!options.mptcp_enabled_, "MPTCP is only supported on Linux");
  int flags = 0;
#else
  int flags = SOCK_NONBLOCK;

  if (options.mptcp_enabled_) {
    ASSERT(socket_type == Network::Socket::Type::Stream);
    ASSERT(addr->type() == Network::Address::Type::Ip);
    protocol = IPPROTO_MPTCP;
  }
#endif

  if (socket_type == Network::Socket::Type::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }

  int domain;
  if (addr->type() == Network::Address::Type::Ip) {
    if (ip_version == Network::Address::IpVersion::v6 || Network::Address::forceV6()) {
      domain = AF_INET6;
    } else {
      ASSERT(ip_version == Network::Address::IpVersion::v4);
      domain = AF_INET;
    }
  } else if (addr->type() == Network::Address::Type::Pipe) {
    domain = AF_UNIX;
  } else {
    ASSERT(addr->type() == Network::Address::Type::EnvoyInternal);
    PANIC("not implemented");
    return nullptr;
  }

  const Api::SysCallSocketResult result =
      Api::OsSysCallsSingleton::get().socket(domain, flags, protocol);
  if (!SOCKET_VALID(result.return_value_)) {
    ENVOY_LOG(error, "socket(2) failed, got error: {}", errorDetails(result.errno_));
    return nullptr;
  }

  os_fd_t sock_fd = result.return_value_;

  // Apply IPv6 socket options if needed (same as socket_interface_impl.cc)
  if (addr->type() == Network::Address::Type::Ip && ip_version == Network::Address::IpVersion::v6 && !Network::Address::forceV6()) {
    // Setting IPV6_V6ONLY restricts the IPv6 socket to IPv6 connections only.
    const Api::SysCallIntResult v6only_result = Api::OsSysCallsSingleton::get().setsockopt(
        sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
    if (SOCKET_FAILURE(v6only_result.return_value_)) {
      ENVOY_LOG(error, "Unable to set socket v6-only: got error: {}", v6only_result.return_value_);
      Api::OsSysCallsSingleton::get().close(sock_fd);
      return nullptr;
    }
  }

#if defined(__APPLE__) || defined(WIN32)
  // Cannot set SOCK_NONBLOCK as a ::socket flag.
  // We need to set non-blocking mode manually for these platforms
  // This is handled in the custom IO handle constructor
#endif

  ENVOY_LOG(debug, "Created socket FD={}, wrapping in custom IO handle", sock_fd);

  // Create custom IO handle that won't close the socket
  return createReverseTunnelInitiatorClientIOHandle(sock_fd, addr);
}

Network::IoHandlePtr ReverseTunnelInitiatorClientSocketInterface::createReverseTunnelInitiatorClientIOHandle(
    os_fd_t fd, const Network::Address::InstanceConstSharedPtr& addr) const {
  
  ENVOY_LOG(debug, "Creating ReverseTunnelInitiatorClientIOHandle for fd={}, address={}", 
            fd, addr ? addr->asString() : "null");
  
  return std::make_unique<ReverseTunnelInitiatorClientIOHandle>(fd, addr);
}

Network::IoHandlePtr ReverseTunnelInitiatorClientSocketInterface::socket(
    Network::Socket::Type, Network::Address::Type, 
    Network::Address::IpVersion, bool, 
    const Network::SocketCreationOptions&) const {
  
  ENVOY_LOG(warn, "ReverseTunnelInitiatorClientSocketInterface: socket() called without address - returning nullptr.");
  
  // Reverse connection sockets should always have an address.
  return nullptr;
}

bool ReverseTunnelInitiatorClientSocketInterface::ipFamilySupported(int domain) {
  return domain == AF_INET || domain == AF_INET6;
}

Server::BootstrapExtensionPtr
ReverseTunnelInitiatorClientSocketInterface::createBootstrapExtension(const Protobuf::Message& /*config*/,
                                                                     Server::Configuration::ServerFactoryContext& /*context*/) {
  return std::make_unique<Network::SocketInterfaceExtension>(*this);
}

ProtobufTypes::MessagePtr ReverseTunnelInitiatorClientSocketInterface::createEmptyConfigProto() {
  return std::make_unique<google::protobuf::Empty>();
}

// Register the factory
REGISTER_FACTORY(ReverseTunnelInitiatorClientSocketInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
