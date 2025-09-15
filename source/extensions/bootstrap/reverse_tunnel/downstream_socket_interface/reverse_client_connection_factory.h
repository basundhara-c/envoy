#pragma once

#include "envoy/network/client_connection_factory.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Client connection factory for reverse connection clients.
 * This factory creates ReverseClientConnection instances that prevent closing.
 */
class ReverseClientConnectionFactory : public Network::ClientConnectionFactory {
public:
  ~ReverseClientConnectionFactory() override = default;

  // Config::UntypedFactory
  std::string name() const override { return "reverse_connection_client"; }

  // Network::ClientConnectionFactory
  Network::ClientConnectionPtr createClientConnection(
      Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
      Network::Address::InstanceConstSharedPtr source_address,
      Network::TransportSocketPtr&& transport_socket,
      const Network::ConnectionSocket::OptionsSharedPtr& options,
      const Network::TransportSocketOptionsConstSharedPtr& transport_options) override;
};

DECLARE_FACTORY(ReverseClientConnectionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
