#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/accepted_reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

AcceptedReverseConnectionIOHandle::AcceptedReverseConnectionIOHandle(os_fd_t fd)
    : IoSocketHandleImpl(fd) {
  ENVOY_LOG(debug, "Created AcceptedReverseConnectionIOHandle: fd={}", fd_);
}

Api::IoCallUint64Result AcceptedReverseConnectionIOHandle::close() {
  ENVOY_LOG(debug, "AcceptedReverseConnectionIOHandle: close() called for fd={}, doing nothing to keep socket open",
            fd_);
  
  // Mark that close has been attempted
  close_attempted_.store(true);
  
  // Do nothing - keep the socket open for reuse
  ENVOY_LOG(debug, "AcceptedReverseConnectionIOHandle: socket fd={} kept open for reuse", fd_);
  return Api::IoCallUint64Result{0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
}

bool AcceptedReverseConnectionIOHandle::isOpen() const {
  // Return false if close() has been called, otherwise delegate to base class
  if (close_attempted_.load()) {
    ENVOY_LOG(debug, "AcceptedReverseConnectionIOHandle: isOpen() returning false for fd={} (close was attempted)", fd_);
    return false;
  }
  return IoSocketHandleImpl::isOpen();
}

AcceptedReverseConnectionIOHandle::~AcceptedReverseConnectionIOHandle() {
  ENVOY_LOG(debug, "AcceptedReverseConnectionIOHandle destructor called for fd={}, keeping socket open for reuse", fd_);

  // Store the actual file descriptor before marking it invalid
  os_fd_t actual_fd = fd_;
  
  // Mark the socket as invalid so the base destructor won't close it
  SET_SOCKET_INVALID(fd_);
  
  // The base destructor will see fd_ as invalid and skip the close() call
  ENVOY_LOG(debug, "AcceptedReverseConnectionIOHandle: socket fd={} kept open for reuse", actual_fd);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
