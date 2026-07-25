#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "envoy/common/pure.h"

namespace Envoy {

namespace Http {
class RequestHeaderMap;
} // namespace Http

/**
 * Optional per-request context passed to a ResourceLimit at enforcement time. This allows a
 * ResourceLimit implementation to make admission decisions and keep bookkeeping keyed by request
 * attributes (for example a header value), rather than a single cluster-wide count.
 *
 * The context is only valid for the duration of the canCreate()/incWithContext() call it is passed
 * to. Implementations must not retain the pointers beyond that call. Any member may be null when the
 * caller has no context to provide, in which case a context-aware implementation should behave as if
 * unkeyed.
 */
struct ResourceLimitContext {
  // Downstream request headers for the request driving this resource acquisition, or nullptr if not
  // available at the call site.
  const Http::RequestHeaderMap* request_headers{nullptr};
};

/**
 * A handle for use by any resource managers.
 */
class ResourceLimit {
public:
  virtual ~ResourceLimit() = default;

  /**
   * @return true if the resource can be created.
   */
  virtual bool canCreate() PURE;

  /**
   * @return true if the resource can be created, taking optional per-request context into account.
   * The default implementation ignores the context and delegates to canCreate(), so existing
   * unkeyed implementations need not override it.
   */
  virtual bool canCreate(const ResourceLimitContext&) { return canCreate(); }

  /**
   * Increment the resource count.
   */
  virtual void inc() PURE;

  /**
   * Increment the resource count, taking optional per-request context into account, and return an
   * opaque token identifying this increment. The token must be handed back to decByToken() when the
   * resource is released so a keyed implementation can decrement the same bucket. A returned token
   * of 0 is the sentinel for "untracked" and decByToken(0) is a guaranteed no-op.
   *
   * The default implementation ignores the context, increments via inc(), and returns 0 so existing
   * unkeyed implementations need not override it.
   */
  virtual uint64_t incWithContext(const ResourceLimitContext&) {
    inc();
    return 0;
  }

  /**
   * Decrement the resource count.
   */
  virtual void dec() PURE;

  /**
   * Decrement the resource count by a specific amount.
   */
  virtual void decBy(uint64_t amount) PURE;

  /**
   * Decrement the resource previously reserved by incWithContext(), identified by the token it
   * returned. The default implementation ignores the token (treating a non-zero token as a single
   * unkeyed decrement, and 0 as a no-op) so existing unkeyed implementations need not override it.
   */
  virtual void decByToken(uint64_t token) {
    if (token != 0) {
      dec();
    }
  }

  /**
   * @return the current maximum allowed number of this resource.
   */
  virtual uint64_t max() PURE;

  /**
   * @return the current resource count.
   */
  virtual uint64_t count() const PURE;
};

using ResourceLimitOptRef = std::optional<std::reference_wrapper<ResourceLimit>>;
using ResourceLimitPtr = std::unique_ptr<ResourceLimit>;

} // namespace Envoy
