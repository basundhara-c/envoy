#pragma once

#include "envoy/network/address.h"
#include "envoy/network/socket.h"

#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

/**
 * Custom address implementation for upstream reverse connections.
 * This address delegates all methods to the base address except socketInterface(),
 * which returns a custom socket interface that creates IO handles
 * that accept reverse connections only.
 */
class UpstreamReverseConnectionAddress : public Network::Address::Instance {
public:
  explicit UpstreamReverseConnectionAddress(Network::Address::InstanceConstSharedPtr base_address);

  // Network::Address::Instance
  bool operator==(const Network::Address::Instance& rhs) const override;
  const std::string& asString() const override;
  absl::string_view asStringView() const override;
  const std::string& logicalName() const override;
  const Network::Address::Ip* ip() const override;
  const Network::Address::Pipe* pipe() const override;
  const Network::Address::EnvoyInternalAddress* envoyInternalAddress() const override;
  const sockaddr* sockAddr() const override;
  socklen_t sockAddrLen() const override;
  Network::Address::Type type() const override;
  absl::string_view addressType() const override;
  const Network::SocketInterface& socketInterface() const override;
  absl::optional<std::string> networkNamespace() const override;

private:
  Network::Address::InstanceConstSharedPtr base_address_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

