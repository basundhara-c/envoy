#pragma once

#include <atomic>
#include <cstdint>

#include "envoy/common/resource.h"

#include "source/extensions/circuit_breakers/dynamic_modules/cb_config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {

// Envoy-side per-request context handed to the module via context_envoy_ptr for a single admission
// decision. Wraps the request headers so the module can read a keying attribute. Lives only on the
// stack for the duration of a canCreate()/incWithContext() call.
struct DynamicModuleCircuitBreakerContext {
  const Http::RequestHeaderMap* request_headers{nullptr};
};

/**
 * A ResourceLimit whose decisions are delegated to a dynamic module over the C ABI. Shared across
 * all worker threads for the life of the cluster, so it holds its own shared_ptr to the config to
 * keep the module loaded, and relies on the module to synchronize its own state.
 */
class DynamicModuleResourceLimit : public ResourceLimit {
public:
  DynamicModuleResourceLimit(DynamicModuleCircuitBreakerConfigSharedPtr config,
                             envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module,
                             uint64_t max)
      : config_(std::move(config)), cb_module_(cb_module), max_(max) {}
  ~DynamicModuleResourceLimit() override {
    if (cb_module_ != nullptr) {
      config_->on_destroy_(cb_module_);
    }
  }

  // Envoy::ResourceLimit
  bool canCreate() override {
    return config_->on_can_create_(cb_module_, /*context_envoy_ptr=*/nullptr);
  }
  bool canCreate(const ResourceLimitContext& context) override {
    DynamicModuleCircuitBreakerContext ctx{context.request_headers};
    return config_->on_can_create_(cb_module_, static_cast<void*>(&ctx));
  }
  void inc() override { incWithContext(ResourceLimitContext{}); }
  uint64_t incWithContext(const ResourceLimitContext& context) override {
    count_.fetch_add(1, std::memory_order_relaxed);
    DynamicModuleCircuitBreakerContext ctx{context.request_headers};
    return config_->on_inc_(cb_module_, static_cast<void*>(&ctx));
  }
  void dec() override { decByToken(0); }
  void decBy(uint64_t amount) override {
    // The token-based contract reserves one unit per inc, so decBy is not part of the keyed path.
    // Fall back to unit decrements against the module's untracked bucket.
    for (uint64_t i = 0; i < amount; i++) {
      decByToken(0);
    }
  }
  void decByToken(uint64_t token) override {
    if (count_.load(std::memory_order_relaxed) > 0) {
      count_.fetch_sub(1, std::memory_order_relaxed);
    }
    config_->on_dec_(cb_module_, token);
  }
  uint64_t max() override { return max_; }
  uint64_t count() const override { return count_.load(std::memory_order_relaxed); }

private:
  const DynamicModuleCircuitBreakerConfigSharedPtr config_;
  const envoy_dynamic_module_type_circuit_breaker_module_ptr cb_module_;
  const uint64_t max_;
  // Approximate cluster-wide count for the count() accessor / stats. The authoritative keyed
  // counters live in the module; this mirrors the built-in relaxed-atomic, overshoot-tolerant
  // contract.
  std::atomic<uint64_t> count_{0};
};

} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
