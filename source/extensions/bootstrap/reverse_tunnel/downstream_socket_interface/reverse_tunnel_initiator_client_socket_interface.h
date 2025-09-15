#pragma once

#include "envoy/network/socket_interface.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/network/socket_interface_impl.h"
#include "google/protobuf/empty.pb.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom socket interface for reverse tunnel initiator client connections.
 * This interface creates ReverseTunnelInitiatorClientIOHandle instances that
 * don't close sockets to allow for reuse in reverse tunnel scenarios.
 */
class ReverseTunnelInitiatorClientSocketInterface : public Network::SocketInterfaceBase,
                                                   public Logger::Loggable<Logger::Id::misc> {
public:
  ReverseTunnelInitiatorClientSocketInterface() = default;

  // Network::SocketInterface
  Network::IoHandlePtr socket(Network::Socket::Type, Network::Address::Type, 
                             Network::Address::IpVersion, bool, 
                             const Network::SocketCreationOptions&) const override;
  Network::IoHandlePtr socket(Network::Socket::Type socket_type, const Network::Address::InstanceConstSharedPtr addr,
                             const Network::SocketCreationOptions& options) const override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr createBootstrapExtension(const Protobuf::Message& config,
                                                        Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return "envoy.bootstrap.reverse_tunnel.initiator_client_socket_interface"; }

private:
  // Create the custom IO handle for reverse tunnel initiator client connections
  Network::IoHandlePtr createReverseTunnelInitiatorClientIOHandle(
      os_fd_t fd, const Network::Address::InstanceConstSharedPtr& addr) const;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
