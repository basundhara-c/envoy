#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/resource.h"
#include "envoy/config/typed_config.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

// The circuit-breaker resource dimension a factory-produced ResourceLimit governs. Passed to the
// factory so a single extension can decide behavior per dimension, or reject dimensions it does not
// support. The retries dimension is intentionally not pluggable: it is backed by a retry budget
// whose limit is computed from other dimensions, so it is out of scope for this extension point.
enum class CircuitBreakerResource {
  Connections,
  PendingRequests,
  Requests,
  ConnectionPools,
};

// Parameters describing the resource a factory is being asked to build a ResourceLimit for. Bundles
// the classic inputs (max, runtime key, stats gauges) so a custom implementation can mirror the
// built-in behavior or ignore them.
struct CircuitBreakerResourceParams {
  CircuitBreakerResource resource;
  uint64_t max;
  Runtime::Loader& runtime;
  std::string runtime_key;
  // Gauge set to 1 when the breaker is open (cannot create), 0 otherwise.
  Stats::Gauge& open_gauge;
  // Gauge tracking how many resources remain before the breaker opens.
  Stats::Gauge& remaining_gauge;
};

/**
 * A factory for custom circuit-breaker ResourceLimit implementations. Registered extensions let a
 * cluster substitute the built-in per-dimension counter with a custom one via TypedExtensionConfig
 * in the cluster's circuit-breaker thresholds.
 *
 * The returned ResourceLimit is shared across all worker threads for the cluster, so
 * implementations must be thread-safe (like the built-in atomic counter).
 */
class CircuitBreakerFactory : public Config::TypedFactory {
public:
  ~CircuitBreakerFactory() override = default;

  /**
   * Create a ResourceLimit for the given resource dimension. Returning nullptr means "use the
   * built-in limit for this dimension" so an extension can opt out of dimensions it does not
   * handle. The loader calls this once per dimension for each configured extension.
   */
  virtual Envoy::ResourceLimitPtr
  createResourceLimit(const Protobuf::Message& config, const CircuitBreakerResourceParams& params,
                      Server::Configuration::ServerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.circuit_breakers"; }
};

} // namespace Upstream
} // namespace Envoy
