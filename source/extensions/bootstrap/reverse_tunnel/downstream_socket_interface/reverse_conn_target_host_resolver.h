#pragma once

#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_conn_target_host_address.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom address resolver that can create ReverseConnTargetHostAddress instances
 * when target host addresses are resolved for reverse tunnel connections.
 */
class ReverseConnTargetHostResolver : public Network::Address::Resolver {
public:
  ReverseConnTargetHostResolver() = default;

  // Network::Address::Resolver
  absl::StatusOr<Network::Address::InstanceConstSharedPtr>
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override;

  std::string name() const override { return "envoy.resolvers.reverse_connection_target_host"; }

  // Friend class for testing
  friend class ReverseConnTargetHostResolverTest;
};

DECLARE_FACTORY(ReverseConnTargetHostResolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
