#include "source/extensions/circuit_breakers/dynamic_modules/config.h"

#include "envoy/extensions/circuit_breakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/circuit_breakers/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/circuit_breakers/dynamic_modules/cb_config.h"
#include "source/extensions/circuit_breakers/dynamic_modules/resource_limit.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {

Envoy::ResourceLimitPtr DynamicModuleCircuitBreakerFactory::createResourceLimit(
    const Protobuf::Message& config, const Upstream::CircuitBreakerResourceParams& params,
    Server::Configuration::ServerFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::circuit_breakers::dynamic_modules::v3::DynamicModuleCircuitBreaker&>(
      config, context.messageValidationContext().staticValidationVisitor());

  // Circuit breakers do not support remote module sources: only synchronous local-file and by-name
  // loads succeed here.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), proto_config.breaker_name(), context);
  if (!load_result.ok()) {
    ENVOY_LOG_MISC(error, "Failed to load dynamic module circuit breaker: {}",
                   load_result.status().message());
    throw EnvoyException(std::string(load_result.status().message()));
  }
  auto dynamic_module = std::move(load_result->loaded);

  std::string breaker_config_str;
  if (proto_config.has_breaker_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.breaker_config());
    THROW_IF_NOT_OK_REF(config_or_error.status());
    breaker_config_str = std::move(config_or_error.value());
  }

  auto cb_config = newDynamicModuleCircuitBreakerConfig(
      proto_config.breaker_name(), breaker_config_str, std::move(dynamic_module));
  THROW_IF_NOT_OK_REF(cb_config.status());

  // Ask the module for an instance for this dimension. A null return means the module declines the
  // dimension, so the built-in limit is kept.
  envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module =
      cb_config.value()->on_new_(cb_config.value()->in_module_config_, params.max);
  if (cb_module == nullptr) {
    return nullptr;
  }
  return std::make_unique<DynamicModuleResourceLimit>(cb_config.value(), cb_module, params.max);
}

ProtobufTypes::MessagePtr DynamicModuleCircuitBreakerFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::circuit_breakers::dynamic_modules::v3::DynamicModuleCircuitBreaker>();
}

REGISTER_FACTORY(DynamicModuleCircuitBreakerFactory, Upstream::CircuitBreakerFactory);

} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
