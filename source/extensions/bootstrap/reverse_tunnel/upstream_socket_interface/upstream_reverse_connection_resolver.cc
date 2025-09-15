#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_reverse_connection_resolver.h"

#include "absl/strings/str_split.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

absl::StatusOr<Network::Address::InstanceConstSharedPtr>
UpstreamReverseConnectionResolver::resolve(const envoy::config::core::v3::SocketAddress& socket_address) {

  ENVOY_LOG_MISC(debug, "UpstreamReverseConnectionResolver::resolve()");

  // Create base address from the regular socket address
  auto base_address = Network::Utility::parseInternetAddressNoThrow(socket_address.address(), socket_address.port_value());
  if (!base_address) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to parse address '{}:{}'", socket_address.address(), 
                   socket_address.port_value()));
  }

  // Create and return UpstreamReverseConnectionAddress
  auto upstream_reverse_conn_address =
      std::make_shared<UpstreamReverseConnectionAddress>(Network::Address::InstanceConstSharedPtr(base_address));

  return upstream_reverse_conn_address;
}


// Register the factory
REGISTER_FACTORY(UpstreamReverseConnectionResolver, Network::Address::Resolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
