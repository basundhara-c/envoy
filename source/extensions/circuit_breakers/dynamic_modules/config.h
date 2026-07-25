#pragma once

#include "envoy/upstream/resource_manager_factory.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {

constexpr char DynamicModuleCircuitBreakerName[] = "envoy.circuit_breakers.dynamic_modules";

/**
 * Config registration for the dynamic module circuit breaker.
 */
class DynamicModuleCircuitBreakerFactory : public Upstream::CircuitBreakerFactory {
public:
  Envoy::ResourceLimitPtr
  createResourceLimit(const Protobuf::Message& config,
                      const Upstream::CircuitBreakerResourceParams& params,
                      Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return DynamicModuleCircuitBreakerName; }
};

DECLARE_FACTORY(DynamicModuleCircuitBreakerFactory);

} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
