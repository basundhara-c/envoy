#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_conn_target_host_resolver.h"

#include "absl/strings/str_split.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

absl::StatusOr<Network::Address::InstanceConstSharedPtr>
ReverseConnTargetHostResolver::resolve(const envoy::config::core::v3::SocketAddress& socket_address) {

  ENVOY_LOG_MISC(debug, "ReverseConnTargetHostResolver::resolve()");

  // Create base address from the regular socket address
  auto base_address = Network::Utility::parseInternetAddressNoThrow(socket_address.address(), socket_address.port_value());
  if (!base_address) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to parse address '{}:{}'", socket_address.address(), 
                   socket_address.port_value()));
  }

  // Create and return ReverseConnTargetHostAddress
  auto reverse_conn_target_host_address =
      std::make_shared<ReverseConnTargetHostAddress>(Network::Address::InstanceConstSharedPtr(base_address));

  return reverse_conn_target_host_address;
}


// Register the factory
REGISTER_FACTORY(ReverseConnTargetHostResolver, Network::Address::Resolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
