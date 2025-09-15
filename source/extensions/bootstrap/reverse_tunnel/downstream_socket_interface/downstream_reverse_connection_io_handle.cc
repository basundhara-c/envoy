#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"

#include "source/common/common/logger.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/common/network/io_socket_error_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// DownstreamReverseConnectionIOHandle constructor implementation
DownstreamReverseConnectionIOHandle::DownstreamReverseConnectionIOHandle(
    Network::ConnectionSocketPtr socket, ReverseConnectionIOHandle* parent,
    const std::string& connection_key)
    : IoSocketHandleImpl(socket->ioHandle().fdDoNotUse()), owned_socket_(std::move(socket)),
      parent_(parent), connection_key_(connection_key) {
  ENVOY_LOG(debug,
            "DownstreamReverseConnectionIOHandle: taking ownership of socket with FD: {} for "
            "connection key: {}",
            fd_, connection_key_);
            
  // Log the socket state to help debug the close detection issue
  ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle: socket state - isOpen={}, fd={}", 
            owned_socket_->isOpen(), fd_);
}

// DownstreamReverseConnectionIOHandle destructor implementation
DownstreamReverseConnectionIOHandle::~DownstreamReverseConnectionIOHandle() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: destroying handle for FD: {} with connection key: {}",
      fd_, connection_key_);
}

// DownstreamReverseConnectionIOHandle close() implementation.
Api::IoCallUint64Result DownstreamReverseConnectionIOHandle::close() {
  ENVOY_LOG(
      debug,
      "DownstreamReverseConnectionIOHandle: closing handle for FD: {} with connection key: {}",
      fd_, connection_key_);

  // If we're ignoring close calls during socket hand-off, just return success.
  if (ignore_close_and_shutdown_) {
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: ignoring close() call during socket hand-off for "
        "connection key: {}",
        connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Prevent double-closing by checking if already closed
  if (fd_ < 0) {
    ENVOY_LOG(debug,
              "DownstreamReverseConnectionIOHandle: handle already closed for connection key: {}",
              connection_key_);
    return Api::ioCallUint64ResultNoError();
  }

  // Notify parent that this downstream connection has been closed
  // This will trigger re-initiation of the reverse connection if needed.
  if (parent_) {
    // parent_->onDownstreamConnectionClosed(connection_key_);
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: notified parent of connection closure for key: {}",
        connection_key_);
  }

  // // Reset the owned socket to properly close the connection.
  // if (owned_socket_) {
  //   owned_socket_.reset();
  // }
  return Api::ioCallUint64ResultNoError();
}

// DownstreamReverseConnectionIOHandle shutdown() implementation.
Api::SysCallIntResult DownstreamReverseConnectionIOHandle::shutdown(int how) {
  ENVOY_LOG(trace,
            "DownstreamReverseConnectionIOHandle: shutdown({}) called for FD: {} with connection "
            "key: {}",
            how, fd_, connection_key_);

  // If we're ignoring shutdown calls during socket hand-off, just return success.
  if (ignore_close_and_shutdown_) {
    ENVOY_LOG(
        debug,
        "DownstreamReverseConnectionIOHandle: ignoring shutdown() call during socket hand-off "
        "for connection key: {}",
        connection_key_);
    return Api::SysCallIntResult{0, 0};
  }

  return IoSocketHandleImpl::shutdown(how);
}

void DownstreamReverseConnectionIOHandle::initializeFileEvent(Event::Dispatcher& dispatcher, 
                                                             Event::FileReadyCb cb,
                                                             Event::FileTriggerType trigger, 
                                                             uint32_t events) {
  ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle::initializeFileEvent: called for fd={}, events={}", 
            fd_, events);
  
  // Filter out CLOSED events to prevent immediate close detection
  // This is needed because the duplicated socket inherits the closed state
  // from the original socket when it's marked as invalid
  uint32_t filtered_events = events & ~Event::FileReadyType::Closed;
  
  ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle::initializeFileEvent: filtered events from {} to {} (removed CLOSED)", 
            events, filtered_events);
  
  // Call the base class implementation with filtered events
  IoSocketHandleImpl::initializeFileEvent(dispatcher, cb, trigger, filtered_events);
}

Api::IoCallUint64Result DownstreamReverseConnectionIOHandle::recv(void* buffer, size_t length, int flags) {
  ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle::recv: called for fd={}, length={}, flags={}",
            fd_, length, flags);
  
  auto result = IoSocketHandleImpl::recv(buffer, length, flags);
  ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle::recv: result - return_value={}, ok={}",
            result.return_value_, result.ok());
  
  // If recv returns 0 (remote close), this is likely a false positive due to socket duplication
  // Both the original and duplicated FDs point to the same kernel socket structure
  if (result.ok() && result.return_value_ == 0) {
    ENVOY_LOG(debug, "DownstreamReverseConnectionIOHandle::recv: detected potential remote close, treating as false positive due to socket duplication");
    
    // Always return EAGAIN for duplicated sockets when recv returns 0
    // This prevents the listener filter from thinking the connection is closed
    return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
  }
  
  return result;
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
