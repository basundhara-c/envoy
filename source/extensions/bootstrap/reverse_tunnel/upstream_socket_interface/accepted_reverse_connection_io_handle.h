#pragma once

#include <atomic>

#include "envoy/network/io_handle.h"

#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * IO handle for accepted reverse connection sockets.
 * This handle overrides the close() method to prevent socket closure,
 * allowing the socket to be reused for reverse connections.
 */
class AcceptedReverseConnectionIOHandle : public Network::IoSocketHandleImpl {
public:
  /**
   * Constructor for AcceptedReverseConnectionIOHandle.
   * @param fd the file descriptor for the accepted socket.
   */
  explicit AcceptedReverseConnectionIOHandle(os_fd_t fd);

  /**
   * Override of close method for accepted reverse connection.
   * This method marks the socket as "closed" for connection logic
   * but keeps the actual socket open for reuse.
   * @return IoCallUint64Result with success status.
   */
  Api::IoCallUint64Result close() override;

  /**
   * Override of isOpen method to return false after close() is called.
   * This prevents the connection from trying to close the socket again.
   * @return false after close() has been called, true otherwise.
   */
  bool isOpen() const override;

  /**
   * Override destructor to prevent socket closure.
   * This ensures the socket remains open for reuse when the handle is destroyed.
   */
  ~AcceptedReverseConnectionIOHandle() override;

private:
  mutable std::atomic<bool> close_attempted_{false};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
