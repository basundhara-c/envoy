#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_client_connection_factory.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_client_connection.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

Network::ClientConnectionPtr ReverseClientConnectionFactory::createClientConnection(
    Event::Dispatcher& dispatcher, Network::Address::InstanceConstSharedPtr address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  
  ENVOY_LOG_MISC(debug, "ReverseClientConnectionFactory::createClientConnection() creating reverse client connection");
  
  return std::make_unique<ReverseClientConnection>(
      dispatcher, address, source_address, std::move(transport_socket), options, transport_options);
}

REGISTER_FACTORY(ReverseClientConnectionFactory, Network::ClientConnectionFactory);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
