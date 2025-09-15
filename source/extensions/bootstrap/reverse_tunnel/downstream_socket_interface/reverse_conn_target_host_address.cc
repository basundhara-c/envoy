#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_conn_target_host_address.h"

#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

ReverseConnTargetHostAddress::ReverseConnTargetHostAddress(
    Network::Address::InstanceConstSharedPtr base_address)
    : base_address_(base_address) {
      ENVOY_LOG_MISC(debug, "ReverseConnTargetHostAddress::ReverseConnTargetHostAddress()");
    }

bool ReverseConnTargetHostAddress::operator==(const Network::Address::Instance& rhs) const {
  const auto* upstream_addr = dynamic_cast<const ReverseConnTargetHostAddress*>(&rhs);
  if (upstream_addr) {
    return base_address_->operator==(*upstream_addr->base_address_);
  }
  return base_address_->operator==(rhs);
}

const Network::Address::Ip* ReverseConnTargetHostAddress::ip() const {
  return base_address_->ip();
}

const Network::Address::Pipe* ReverseConnTargetHostAddress::pipe() const {
  return base_address_->pipe();
}

const Network::Address::EnvoyInternalAddress* 
ReverseConnTargetHostAddress::envoyInternalAddress() const {
  return base_address_->envoyInternalAddress();
}

const sockaddr* ReverseConnTargetHostAddress::sockAddr() const {
  return base_address_->sockAddr();
}

socklen_t ReverseConnTargetHostAddress::sockAddrLen() const {
  return base_address_->sockAddrLen();
}

Network::Address::Type ReverseConnTargetHostAddress::type() const {
  return base_address_->type();
}

const std::string& ReverseConnTargetHostAddress::logicalName() const {
  return base_address_->logicalName();
}

const std::string& ReverseConnTargetHostAddress::asString() const {
  return base_address_->asString();
}

absl::string_view ReverseConnTargetHostAddress::asStringView() const {
  return base_address_->asStringView();
}

absl::string_view ReverseConnTargetHostAddress::addressType() const {
  return "reverse_connection_client";
}

const Network::SocketInterface& ReverseConnTargetHostAddress::socketInterface() const {
  ENVOY_LOG_MISC(debug, "ReverseConnTargetHostAddress::socketInterface()");
  // Return the custom socket interface for reverse tunnel initiator client connections
  auto* interface = Network::socketInterface("envoy.bootstrap.reverse_tunnel.initiator_client_socket_interface");
  if (interface) {
    ENVOY_LOG_MISC(debug, "ReverseConnTargetHostAddress::socketInterface() found custom interface");
    return *interface;
  }
  ENVOY_LOG_MISC(debug, "ReverseConnTargetHostAddress::socketInterface() no custom interface found");
  // Fallback to default socket interface if custom interface is not available
  return *Network::socketInterface("envoy.extensions.network.socket_interface.default_socket_interface");
}

absl::optional<std::string> ReverseConnTargetHostAddress::networkNamespace() const {
  return base_address_->networkNamespace();
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
