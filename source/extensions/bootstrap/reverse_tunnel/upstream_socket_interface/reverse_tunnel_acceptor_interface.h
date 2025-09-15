#pragma once

#include <memory>
#include <string>

#include "envoy/network/socket.h"
#include "envoy/network/socket_interface.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_reverse_connection_address.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Forward declarations
class ReverseConnectionAcceptorIOHandle;

/**
 * Socket interface that creates upstream reverse connection acceptor sockets.
 * This interface creates IO handles that override the close() method to prevent
 * socket closure, allowing the socket to be reused for reverse connections.
 */
class ReverseTunnelAcceptorInterface : public Envoy::Network::SocketInterfaceBase,
                                      public Envoy::Logger::Loggable<Envoy::Logger::Id::connection> {
  // Friend class for testing
  friend class ReverseTunnelAcceptorInterfaceTest;

public:
  /**
   * Constructs a ReverseTunnelAcceptorInterface.
   */
  ReverseTunnelAcceptorInterface();

  // SocketInterface overrides
  /**
   * Create a socket without a specific address (no-op for reverse connections).
   * @param socket_type the type of socket to create.
   * @param addr_type the address type.
   * @param version the IP version.
   * @param socket_v6only whether to create IPv6-only socket.
   * @param options socket creation options.
   * @return nullptr since reverse connections require specific addresses.
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type, Envoy::Network::Address::Type addr_type,
         Envoy::Network::Address::IpVersion version, bool socket_v6only,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * Create a socket with a specific address.
   * @param socket_type the type of socket to create.
   * @param addr the address to bind to.
   * @param options socket creation options.
   * @return IoHandlePtr for the reverse connection acceptor socket.
   */
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr,
         const Envoy::Network::SocketCreationOptions& options) const override;

  /**
   * @return true if the IP family is supported
   */
  bool ipFamilySupported(int domain) override;

  std::string name() const override {
    return "envoy.bootstrap.reverse_tunnel.upstream_socket_interface.acceptor";
  }

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
