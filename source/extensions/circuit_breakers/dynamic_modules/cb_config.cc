#include "source/extensions/circuit_breakers/dynamic_modules/cb_config.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {

DynamicModuleCircuitBreakerConfig::DynamicModuleCircuitBreakerConfig(
    absl::string_view breaker_name, absl::string_view breaker_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module)
    : breaker_name_(breaker_name), breaker_config_(breaker_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleCircuitBreakerConfig::~DynamicModuleCircuitBreakerConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

absl::StatusOr<DynamicModuleCircuitBreakerConfigSharedPtr>
newDynamicModuleCircuitBreakerConfig(absl::string_view breaker_name,
                                     absl::string_view breaker_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto on_config_new = dynamic_module->getFunctionPointer<OnCircuitBreakerConfigNewType>(
      "envoy_dynamic_module_on_circuit_breaker_config_new");
  RETURN_IF_NOT_OK_REF(on_config_new.status());
  auto on_config_destroy = dynamic_module->getFunctionPointer<OnCircuitBreakerConfigDestroyType>(
      "envoy_dynamic_module_on_circuit_breaker_config_destroy");
  RETURN_IF_NOT_OK_REF(on_config_destroy.status());
  auto on_new = dynamic_module->getFunctionPointer<OnCircuitBreakerNewType>(
      "envoy_dynamic_module_on_circuit_breaker_new");
  RETURN_IF_NOT_OK_REF(on_new.status());
  auto on_can_create = dynamic_module->getFunctionPointer<OnCircuitBreakerCanCreateType>(
      "envoy_dynamic_module_on_circuit_breaker_can_create");
  RETURN_IF_NOT_OK_REF(on_can_create.status());
  auto on_inc = dynamic_module->getFunctionPointer<OnCircuitBreakerIncType>(
      "envoy_dynamic_module_on_circuit_breaker_inc");
  RETURN_IF_NOT_OK_REF(on_inc.status());
  auto on_dec = dynamic_module->getFunctionPointer<OnCircuitBreakerDecType>(
      "envoy_dynamic_module_on_circuit_breaker_dec");
  RETURN_IF_NOT_OK_REF(on_dec.status());
  auto on_destroy = dynamic_module->getFunctionPointer<OnCircuitBreakerDestroyType>(
      "envoy_dynamic_module_on_circuit_breaker_destroy");
  RETURN_IF_NOT_OK_REF(on_destroy.status());

  auto config = std::make_shared<DynamicModuleCircuitBreakerConfig>(breaker_name, breaker_config,
                                                                    std::move(dynamic_module));
  config->on_config_destroy_ = on_config_destroy.value();
  config->on_new_ = on_new.value();
  config->on_can_create_ = on_can_create.value();
  config->on_inc_ = on_inc.value();
  config->on_dec_ = on_dec.value();
  config->on_destroy_ = on_destroy.value();

  envoy_dynamic_module_type_envoy_buffer name_buf = {config->breaker_name_.data(),
                                                     config->breaker_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buf = {config->breaker_config_.data(),
                                                       config->breaker_config_.size()};
  config->in_module_config_ =
      (*on_config_new.value())(static_cast<void*>(config.get()), name_buf, config_buf);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to initialize dynamic module circuit breaker config");
  }
  return config;
}

} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
