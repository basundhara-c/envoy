#include <stddef.h>
#include <stdint.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// Minimal circuit breaker module implementing every required symbol. The config-new and new hooks
// return non-null sentinels so the factory succeeds; the admission hooks always admit.

static int config_sentinel = 0;
static int breaker_sentinel = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_circuit_breaker_config_module_ptr
envoy_dynamic_module_on_circuit_breaker_config_new(
    envoy_dynamic_module_type_circuit_breaker_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_sentinel;
}

void envoy_dynamic_module_on_circuit_breaker_config_destroy(
    envoy_dynamic_module_type_circuit_breaker_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_circuit_breaker_module_ptr envoy_dynamic_module_on_circuit_breaker_new(
    envoy_dynamic_module_type_circuit_breaker_config_module_ptr config_module_ptr, uint64_t max) {
  (void)config_module_ptr;
  (void)max;
  return &breaker_sentinel;
}

bool envoy_dynamic_module_on_circuit_breaker_can_create(
    envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module_ptr,
    envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr context_envoy_ptr) {
  (void)cb_module_ptr;
  (void)context_envoy_ptr;
  return true;
}

uint64_t envoy_dynamic_module_on_circuit_breaker_inc(
    envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module_ptr,
    envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr context_envoy_ptr) {
  (void)cb_module_ptr;
  (void)context_envoy_ptr;
  return 0;
}

void envoy_dynamic_module_on_circuit_breaker_dec(
    envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module_ptr, uint64_t token) {
  (void)cb_module_ptr;
  (void)token;
}

void envoy_dynamic_module_on_circuit_breaker_destroy(
    envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module_ptr) {
  (void)cb_module_ptr;
}
