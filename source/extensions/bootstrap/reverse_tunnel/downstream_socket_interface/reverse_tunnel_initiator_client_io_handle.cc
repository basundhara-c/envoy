#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_client_io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseTunnelInitiatorClientIOHandle::ReverseTunnelInitiatorClientIOHandle(
    os_fd_t fd, const Network::Address::InstanceConstSharedPtr& address)
    : IoSocketHandleImpl(fd), address_(address) {
  ENVOY_LOG(debug, "Created ReverseTunnelInitiatorClientIOHandle: fd={}, address={}", 
            fd_, address ? address->asString() : "null");
}

Api::IoCallUint64Result ReverseTunnelInitiatorClientIOHandle::close() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorClientIOHandle: close() called for fd={}, doing nothing to keep socket open for reuse",
            fd_);
  
  // Mark that close has been attempted
  close_attempted_.store(true);
  
  // Do nothing - keep the socket open for reuse
  ENVOY_LOG(debug, "ReverseTunnelInitiatorClientIOHandle: socket fd={} kept open for reuse", fd_);
  return Api::IoCallUint64Result{0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
}

bool ReverseTunnelInitiatorClientIOHandle::isOpen() const {
  // Return false if close() has been called, otherwise delegate to base class
  // if (close_attempted_.load()) {
  //   ENVOY_LOG(debug, "ReverseTunnelInitiatorClientIOHandle: isOpen() returning false for fd={} (close was attempted)", fd_);
  //   return false;
  // }
  return IoSocketHandleImpl::isOpen();
}

ReverseTunnelInitiatorClientIOHandle::~ReverseTunnelInitiatorClientIOHandle() {
  ENVOY_LOG(debug, "ReverseTunnelInitiatorClientIOHandle destructor called for fd={}, keeping socket open for reuse", fd_);

  // Store the actual file descriptor before marking it invalid
  os_fd_t actual_fd = fd_;
  
  // Mark the socket as invalid so the base destructor won't close it
  // This just sets fd_ = -1, which won't trigger connection events
  SET_SOCKET_INVALID(fd_);
  
  // Reset file events to prevent further I/O operations
  if (file_event_) {
    file_event_.reset();
  }
  
  // The base destructor will see fd_ as invalid and skip the close() call
  ENVOY_LOG(debug, "ReverseTunnelInitiatorClientIOHandle: socket fd={} kept open for reuse", actual_fd);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
