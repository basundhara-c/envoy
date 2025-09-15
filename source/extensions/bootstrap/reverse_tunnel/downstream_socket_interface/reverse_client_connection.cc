#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_client_connection.h"

#include "source/common/network/connection_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseClientConnection::ReverseClientConnection(
    Event::Dispatcher& dispatcher,
    const Network::Address::InstanceConstSharedPtr& remote_address,
    const Network::Address::InstanceConstSharedPtr& source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options)
    : Network::ClientConnectionImpl(dispatcher, remote_address, source_address,
                                   std::move(transport_socket), options, transport_options) {
  ENVOY_LOG_MISC(debug, "ReverseClientConnection::ReverseClientConnection()");
}

ReverseClientConnection::ReverseClientConnection(
    Event::Dispatcher& dispatcher, std::unique_ptr<Network::ConnectionSocket> socket,
    const Network::Address::InstanceConstSharedPtr& source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options)
    : Network::ClientConnectionImpl(dispatcher, std::move(socket), source_address,
                                   std::move(transport_socket), options, transport_options) {
  ENVOY_LOG_MISC(debug, "ReverseClientConnection::ReverseClientConnection() with socket");
}

ReverseClientConnection::~ReverseClientConnection() {
  ENVOY_LOG_MISC(debug, "ReverseClientConnection::~ReverseClientConnection() - preventing base class close() call");
}

void ReverseClientConnection::closeSocket(Network::ConnectionEvent close_type) {
  ENVOY_LOG_MISC(debug, "ReverseClientConnection::closeSocket() called with type {} - preventing socket closure", 
                 static_cast<int>(close_type));
  
  // Mark the connection as requested for closure but don't actually close the socket
  marked_for_closure_ = true;
  
  // Log the close request but don't call the parent's closeSocket method
  ENVOY_LOG_MISC(debug, "ReverseClientConnection::closeSocket() - socket marked for closure but kept open");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
