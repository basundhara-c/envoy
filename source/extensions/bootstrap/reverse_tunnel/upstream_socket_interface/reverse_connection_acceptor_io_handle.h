#pragma once

#include "envoy/network/io_handle.h"

#include "source/common/network/io_socket_handle_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/accepted_reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * IO handle for upstream reverse connection acceptor sockets.
 * This handle overrides the close() method to prevent socket closure,
 * allowing the socket to be reused for reverse connections.
 */
class ReverseConnectionAcceptorIOHandle : public Network::IoSocketHandleImpl {
public:
  /**
   * Constructor for ReverseConnectionAcceptorIOHandle.
   * @param fd the file descriptor for the socket.
   */
  explicit ReverseConnectionAcceptorIOHandle(os_fd_t fd);

  /**
   * Override of accept method for reverse connection acceptor.
   * Creates AcceptedReverseConnectionIOHandle for accepted connections.
   * @param addr pointer to sockaddr structure
   * @param addrlen pointer to socklen_t
   * @return IoHandlePtr for accepted connection
   */
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;

  /**
   * Override of bind method for reverse connection acceptor.
   * Adds custom logging and error handling for reverse connection sockets.
   * @param address the address to bind to
   * @return SysCallIntResult with bind result
   */
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
