#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_acceptor_io_handle.h"

#include "source/common/common/logger.h"
#include "source/common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseConnectionAcceptorIOHandle::ReverseConnectionAcceptorIOHandle(os_fd_t fd)
    : IoSocketHandleImpl(fd) {
  ENVOY_LOG(debug, "Created ReverseConnectionAcceptorIOHandle: fd={}", fd_);
}

Network::IoHandlePtr ReverseConnectionAcceptorIOHandle::accept(struct sockaddr* addr, socklen_t* addrlen) {
  ENVOY_LOG(debug, "ReverseConnectionAcceptorIOHandle: accept() called for fd={}", fd_);
  
  // Call the parent class accept method
  auto result = Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
  if (SOCKET_INVALID(result.return_value_)) {
    return nullptr;
  }
  
  ENVOY_LOG(debug, "ReverseConnectionAcceptorIOHandle: Accepted connection fd={}", result.return_value_);
  
  // Create AcceptedReverseConnectionIOHandle for the accepted connection
  return std::make_unique<AcceptedReverseConnectionIOHandle>(result.return_value_);
}

Api::SysCallIntResult ReverseConnectionAcceptorIOHandle::bind(Network::Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(debug, "ReverseConnectionAcceptorIOHandle: bind() called for fd={} to address {}", 
            fd_, address->asString());
  
  // Call the parent class bind method
  auto result = IoSocketHandleImpl::bind(address);
  
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "ReverseConnectionAcceptorIOHandle: Successfully bound fd={} to {}", 
              fd_, address->asString());
  } else {
    ENVOY_LOG(error, "ReverseConnectionAcceptorIOHandle: Failed to bind fd={} to {}: {}", 
              fd_, address->asString(), errorDetails(result.errno_));
  }
  
  return result;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
