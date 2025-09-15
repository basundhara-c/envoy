#pragma once

#include "envoy/network/resolver.h"
#include "envoy/registry/registry.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_reverse_connection_address.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom address resolver that can create UpstreamReverseConnectionAddress instances
 * when upstream reverse connection metadata is detected in the socket address.
 */
class UpstreamReverseConnectionResolver : public Network::Address::Resolver {
public:
  UpstreamReverseConnectionResolver() = default;

  // Network::Address::Resolver
  absl::StatusOr<Network::Address::InstanceConstSharedPtr>
  resolve(const envoy::config::core::v3::SocketAddress& socket_address) override;

  std::string name() const override { return "envoy.resolvers.upstream_reverse_connection"; }

  // Friend class for testing
  friend class UpstreamReverseConnectionResolverTest;
};

DECLARE_FACTORY(UpstreamReverseConnectionResolver);

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
