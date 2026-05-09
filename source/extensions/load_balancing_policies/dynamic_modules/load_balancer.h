#pragma once

#include <atomic>
#include <memory>

#include "envoy/common/callback.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

class DynamicModuleLoadBalancer;

/**
 * Cross-thread-shared state for a per-worker DynamicModuleLoadBalancer. Held jointly by the LB
 * and any DynamicModuleLoadBalancerScheduler bound to it; the LB clears `lb` in its destructor
 * so foreign-thread schedulers observe a "dead" LB and skip dispatching.
 */
struct DynamicModuleLoadBalancerSharedState {
  // Worker dispatcher this LB lives on. May be nullptr in unit tests that construct an LB
  // without a dispatcher. Set once at construction; never mutated again.
  std::atomic<Event::Dispatcher*> dispatcher{nullptr};
  // The owning LB. Set at construction; cleared by ~DynamicModuleLoadBalancer so that any
  // post() callback that races destruction observes a null pointer and becomes a no-op.
  std::atomic<DynamicModuleLoadBalancer*> lb{nullptr};
};
using DynamicModuleLoadBalancerSharedStatePtr =
    std::shared_ptr<DynamicModuleLoadBalancerSharedState>;
using DynamicModuleLoadBalancerSharedStateWeakPtr =
    std::weak_ptr<DynamicModuleLoadBalancerSharedState>;

/**
 * A load balancer implementation that delegates host selection to a dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer,
                                  public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleLoadBalancer(DynamicModuleLbConfigSharedPtr config,
                            const Upstream::PrioritySet& priority_set,
                            const std::string& cluster_name, Event::Dispatcher* dispatcher);
  ~DynamicModuleLoadBalancer() override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

  // Accessors for callbacks.
  const std::string& clusterName() const { return cluster_name_; }
  const Upstream::PrioritySet& prioritySet() const { return priority_set_; }

  /**
   * Returns the worker dispatcher this LB is running on; safe to call from any thread.
   * Returns nullptr if the LB was created without a dispatcher (e.g. unit tests).
   */
  Event::Dispatcher* dispatcher() const {
    return shared_state_->dispatcher.load(std::memory_order_acquire);
  }

  /**
   * Returns a weak pointer to the shared state for use by DynamicModuleLoadBalancerScheduler.
   * Schedulers may outlive the LB; the weak_ptr lets them detect destruction.
   */
  DynamicModuleLoadBalancerSharedStateWeakPtr weakSharedState() const { return shared_state_; }

  // Per-host custom data storage.
  bool setHostData(uint32_t priority, size_t index, uintptr_t data);
  bool getHostData(uint32_t priority, size_t index, uintptr_t* data) const;

  // Accessors for hosts added/removed during the on_host_membership_update callback.
  const Upstream::HostVector* hostsAdded() const { return hosts_added_; }
  const Upstream::HostVector* hostsRemoved() const { return hosts_removed_; }

  // Worker-thread entry point invoked by DynamicModuleLoadBalancerScheduler::commit() after
  // it has been posted onto this LB's dispatcher.
  void onScheduled(uint64_t event_id);

private:
  DynamicModuleLbConfigSharedPtr config_;
  const Upstream::PrioritySet& priority_set_;
  std::string cluster_name_;
  envoy_dynamic_module_type_lb_module_ptr in_module_lb_;

  // Holds the worker dispatcher and a back-pointer to this LB. Held jointly by any scheduler
  // bound to this LB, so that foreign threads can detect destruction (back-pointer goes to
  // null in ~DynamicModuleLoadBalancer).
  DynamicModuleLoadBalancerSharedStatePtr shared_state_;

  // Handle for the member update callback registration. Automatically unregisters on destruction.
  Envoy::Common::CallbackHandlePtr member_update_cb_;

  // Temporary pointers to host vectors, valid only during on_host_membership_update callback.
  const Upstream::HostVector* hosts_added_{};
  const Upstream::HostVector* hosts_removed_{};

  // Per-host data storage keyed by (priority, index). This is per-LB-instance (per-worker).
  absl::flat_hash_map<std::pair<uint32_t, size_t>, uintptr_t> per_host_data_;
};

/**
 * Used to schedule a cluster LB event hook from a non-worker thread. Created via
 * envoy_dynamic_module_callback_cluster_lb_scheduler_new and deleted via
 * envoy_dynamic_module_callback_cluster_lb_scheduler_delete.
 *
 * Mirrors DynamicModuleHttpFilterScheduler.
 */
class DynamicModuleLoadBalancerScheduler {
public:
  explicit DynamicModuleLoadBalancerScheduler(DynamicModuleLoadBalancerSharedStateWeakPtr state)
      : state_(std::move(state)) {}

  // Safe to call from any thread. Reads only the weak_ptr and the atomic dispatcher / lb cache
  // in the shared state; no direct access to the LB on a foreign thread.
  void commit(uint64_t event_id) {
    DynamicModuleLoadBalancerSharedStatePtr state = state_.lock();
    if (!state) {
      return;
    }
    Event::Dispatcher* dispatcher = state->dispatcher.load(std::memory_order_acquire);
    if (dispatcher == nullptr) {
      return;
    }
    dispatcher->post([state_weak = state_, event_id]() {
      DynamicModuleLoadBalancerSharedStatePtr state_shared = state_weak.lock();
      if (!state_shared) {
        return;
      }
      DynamicModuleLoadBalancer* lb = state_shared->lb.load(std::memory_order_acquire);
      if (lb == nullptr) {
        return;
      }
      lb->onScheduled(event_id);
    });
  }

private:
  DynamicModuleLoadBalancerSharedStateWeakPtr state_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
