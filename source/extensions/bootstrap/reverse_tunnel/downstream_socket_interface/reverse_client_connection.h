#pragma once

#include "envoy/network/connection.h"
#include "source/common/network/connection_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * A client connection implementation that prevents closing for reverse connections.
 * This is used for reverse tunnel connections where the connection should remain
 * open even when close() is called.
 */
class ReverseClientConnection : public Network::ClientConnectionImpl {
public:
  ReverseClientConnection(Event::Dispatcher& dispatcher,
                         const Network::Address::InstanceConstSharedPtr& remote_address,
                         const Network::Address::InstanceConstSharedPtr& source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options,
                         const Network::TransportSocketOptionsConstSharedPtr& transport_options);

  ReverseClientConnection(Event::Dispatcher& dispatcher, std::unique_ptr<Network::ConnectionSocket> socket,
                         const Network::Address::InstanceConstSharedPtr& source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options,
                         const Network::TransportSocketOptionsConstSharedPtr& transport_options);

  // Override destructor to prevent base class from calling close()
  ~ReverseClientConnection() override;

  // Override closeSocket() to prevent the actual socket from closing
  void closeSocket(Network::ConnectionEvent close_type) override;

private:
  // Flag to track if this connection has been marked for closure
  bool marked_for_closure_{false};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
