#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_reverse_connection_address.h"

#include "source/common/network/socket_interface.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

UpstreamReverseConnectionAddress::UpstreamReverseConnectionAddress(
    Network::Address::InstanceConstSharedPtr base_address)
    : base_address_(base_address) {
      ENVOY_LOG_MISC(debug, "UpstreamReverseConnectionAddress::UpstreamReverseConnectionAddress()");
    }

bool UpstreamReverseConnectionAddress::operator==(const Network::Address::Instance& rhs) const {
  const auto* upstream_addr = dynamic_cast<const UpstreamReverseConnectionAddress*>(&rhs);
  if (upstream_addr) {
    return base_address_->operator==(*upstream_addr->base_address_);
  }
  return base_address_->operator==(rhs);
}

const Network::Address::Ip* UpstreamReverseConnectionAddress::ip() const {
  return base_address_->ip();
}

const Network::Address::Pipe* UpstreamReverseConnectionAddress::pipe() const {
  return base_address_->pipe();
}

const Network::Address::EnvoyInternalAddress* 
UpstreamReverseConnectionAddress::envoyInternalAddress() const {
  return base_address_->envoyInternalAddress();
}

const sockaddr* UpstreamReverseConnectionAddress::sockAddr() const {
  return base_address_->sockAddr();
}

socklen_t UpstreamReverseConnectionAddress::sockAddrLen() const {
  return base_address_->sockAddrLen();
}

Network::Address::Type UpstreamReverseConnectionAddress::type() const {
  return base_address_->type();
}

const std::string& UpstreamReverseConnectionAddress::logicalName() const {
  return base_address_->logicalName();
}

const std::string& UpstreamReverseConnectionAddress::asString() const {
  return base_address_->asString();
}

absl::string_view UpstreamReverseConnectionAddress::asStringView() const {
  return base_address_->asStringView();
}

absl::string_view UpstreamReverseConnectionAddress::addressType() const {
  return base_address_->addressType();
}

const Network::SocketInterface& UpstreamReverseConnectionAddress::socketInterface() const {
  ENVOY_LOG_MISC(debug, "UpstreamReverseConnectionAddress::socketInterface()");
  // Return the custom socket interface for reverse connection acceptors
  auto* interface = Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface.acceptor");
  if (interface) {
    ENVOY_LOG_MISC(debug, "UpstreamReverseConnectionAddress::socketInterface() found custom interface");
    return *interface;
  }
  ENVOY_LOG_MISC(debug, "UpstreamReverseConnectionAddress::socketInterface() no custom interface found");
  // Fallback to default socket interface if custom interface is not available
  return *Network::socketInterface("envoy.extensions.network.socket_interface.default_socket_interface");
}

absl::optional<std::string> UpstreamReverseConnectionAddress::networkNamespace() const {
  return base_address_->networkNamespace();
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

