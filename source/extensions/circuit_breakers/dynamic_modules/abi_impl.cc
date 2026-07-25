// NOLINT(namespace-envoy)

// Host-side implementations of the circuit-breaker ABI callbacks. The strong symbols here override
// the weak stubs in source/extensions/dynamic_modules/abi_impl.cc when this extension is linked in.

#include "envoy/http/header_map.h"

#include "source/extensions/circuit_breakers/dynamic_modules/resource_limit.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

using Envoy::Extensions::CircuitBreakers::DynamicModules::DynamicModuleCircuitBreakerContext;

extern "C" {

bool envoy_dynamic_module_callback_circuit_breaker_get_request_header(
    envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer key,
    envoy_dynamic_module_type_envoy_buffer* result_buffer) {
  if (result_buffer == nullptr) {
    return false;
  }
  result_buffer->ptr = nullptr;
  result_buffer->length = 0;

  auto* context = static_cast<DynamicModuleCircuitBreakerContext*>(context_envoy_ptr);
  if (context == nullptr || context->request_headers == nullptr || key.ptr == nullptr) {
    return false;
  }

  const absl::string_view key_view(key.ptr, key.length);
  const auto values = context->request_headers->get(Envoy::Http::LowerCaseString(key_view));
  if (values.empty()) {
    return false;
  }
  const absl::string_view value = values[0]->value().getStringView();
  result_buffer->ptr = const_cast<char*>(value.data());
  result_buffer->length = value.size();
  return true;
}

} // extern "C"
