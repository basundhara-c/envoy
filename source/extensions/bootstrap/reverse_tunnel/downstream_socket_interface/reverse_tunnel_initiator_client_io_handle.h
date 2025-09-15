#pragma once

#include "envoy/network/io_handle.h"

#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom IO handle for reverse tunnel initiator client connections.
 * This IO handle overrides close() and destructor to prevent socket closure,
 * allowing sockets to be reused in reverse tunnel scenarios.
 */
class ReverseTunnelInitiatorClientIOHandle : public Network::IoSocketHandleImpl {
public:
  ReverseTunnelInitiatorClientIOHandle(os_fd_t fd, 
                                      const Network::Address::InstanceConstSharedPtr& address);

  // Network::IoHandle
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;

  // Destructor
  ~ReverseTunnelInitiatorClientIOHandle() override;

  // Accessor for the associated address
  const Network::Address::InstanceConstSharedPtr& address() const { return address_; }

private:
  Network::Address::InstanceConstSharedPtr address_;
  std::atomic<bool> close_attempted_{false};
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
