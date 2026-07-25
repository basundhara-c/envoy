#pragma once

#include <memory>

#include "envoy/extensions/circuit_breakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {

// Type aliases for the function pointers resolved from the module.
using OnCircuitBreakerConfigNewType =
    decltype(&envoy_dynamic_module_on_circuit_breaker_config_new);
using OnCircuitBreakerConfigDestroyType =
    decltype(&envoy_dynamic_module_on_circuit_breaker_config_destroy);
using OnCircuitBreakerNewType = decltype(&envoy_dynamic_module_on_circuit_breaker_new);
using OnCircuitBreakerCanCreateType =
    decltype(&envoy_dynamic_module_on_circuit_breaker_can_create);
using OnCircuitBreakerIncType = decltype(&envoy_dynamic_module_on_circuit_breaker_inc);
using OnCircuitBreakerDecType = decltype(&envoy_dynamic_module_on_circuit_breaker_dec);
using OnCircuitBreakerDestroyType = decltype(&envoy_dynamic_module_on_circuit_breaker_destroy);

/**
 * Holds the loaded dynamic module, the resolved circuit-breaker symbols, and the in-module config
 * pointer. A ResourceLimit produced for each dimension holds its own shared_ptr to this config so
 * the module stays loaded for as long as any limit forwards calls into it.
 *
 * Symbol resolution and in-module config creation happen in newDynamicModuleCircuitBreakerConfig()
 * so failures are handled gracefully rather than throwing from the constructor.
 */
class DynamicModuleCircuitBreakerConfig
    : public std::enable_shared_from_this<DynamicModuleCircuitBreakerConfig> {
public:
  DynamicModuleCircuitBreakerConfig(absl::string_view breaker_name, absl::string_view breaker_config,
                                    Extensions::DynamicModules::DynamicModulePtr dynamic_module);
  ~DynamicModuleCircuitBreakerConfig();

  // The in-module configuration pointer returned by the config-new hook.
  envoy_dynamic_module_type_circuit_breaker_config_module_ptr in_module_config_{nullptr};

  // Resolved function pointers. All are non-null once construction via the factory succeeds.
  OnCircuitBreakerConfigDestroyType on_config_destroy_{nullptr};
  OnCircuitBreakerNewType on_new_{nullptr};
  OnCircuitBreakerCanCreateType on_can_create_{nullptr};
  OnCircuitBreakerIncType on_inc_{nullptr};
  OnCircuitBreakerDecType on_dec_{nullptr};
  OnCircuitBreakerDestroyType on_destroy_{nullptr};

private:
  friend absl::StatusOr<std::shared_ptr<DynamicModuleCircuitBreakerConfig>>
  newDynamicModuleCircuitBreakerConfig(absl::string_view breaker_name,
                                       absl::string_view breaker_config,
                                       Extensions::DynamicModules::DynamicModulePtr dynamic_module);

  const std::string breaker_name_;
  const std::string breaker_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleCircuitBreakerConfigSharedPtr =
    std::shared_ptr<DynamicModuleCircuitBreakerConfig>;

/**
 * Loads the module symbols and creates the in-module configuration.
 * @return the shared config, or an error if a required symbol is missing or config init failed.
 */
absl::StatusOr<DynamicModuleCircuitBreakerConfigSharedPtr>
newDynamicModuleCircuitBreakerConfig(absl::string_view breaker_name,
                                     absl::string_view breaker_config,
                                     Extensions::DynamicModules::DynamicModulePtr dynamic_module);

} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
