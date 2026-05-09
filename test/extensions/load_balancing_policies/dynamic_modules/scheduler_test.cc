#include <atomic>
#include <thread>

#include "source/common/network/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"
#include "source/extensions/load_balancing_policies/dynamic_modules/load_balancer.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {
namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// Per-worker LB scheduler tests. These mirror the HTTP-filter scheduler tests in
// test/extensions/dynamic_modules/http/abi_impl_test.cc so the parallel surface area is exercised
// the same way: a real DynamicModuleLoadBalancer is constructed and bound to a MockDispatcher;
// the ABI scheduler callbacks are then driven directly and the captured dispatcher post is run
// by hand.
class DynamicModuleLoadBalancerSchedulerTest : public testing::Test {
protected:
  DynamicModuleLoadBalancerSchedulerTest() {
    Envoy::Extensions::DynamicModules::DynamicModulesTestEnvironment::setModulesSearchPath();
  }

  void SetUp() override {
    host_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto addr = Network::Utility::parseInternetAddressNoThrow("10.0.0.1", 8080, false);
    ON_CALL(*host_, address()).WillByDefault(Return(addr));
    ON_CALL(*host_, weight()).WillByDefault(Return(1));
    ON_CALL(*host_, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));
    ON_CALL(*host_, locality()).WillByDefault(ReturnRef(default_locality_));

    auto* mock_host_set = priority_set_.getMockHostSet(0);
    mock_host_set->hosts_ = {host_};
    mock_host_set->healthy_hosts_ = {host_};
    ON_CALL(*mock_host_set, hosts()).WillByDefault(ReturnRef(mock_host_set->hosts_));
    ON_CALL(*mock_host_set, healthyHosts())
        .WillByDefault(ReturnRef(mock_host_set->healthy_hosts_));
    ON_CALL(priority_set_, hostSetsPerPriority())
        .WillByDefault(ReturnRef(priority_set_.host_sets_));

    // Build a DynamicModuleLbConfig from the lb_scheduler test module and a worker-bound LB.
    auto module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("lb_scheduler", "c"), false);
    ASSERT_TRUE(module.ok()) << module.status().message();

    auto lb_config_or_status =
        DynamicModuleLbConfig::create("test_lb", "", std::string(DefaultMetricsNamespace),
                                      std::move(module.value()), *stats_store_.rootScope());
    ASSERT_TRUE(lb_config_or_status.ok()) << lb_config_or_status.status().message();
    lb_config_ = lb_config_or_status.value();

    lb_ = std::make_unique<DynamicModuleLoadBalancer>(lb_config_, priority_set_, "test_cluster",
                                                      &worker_dispatcher_);
  }

  void TearDown() override { lb_.reset(); }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Event::MockDispatcher> worker_dispatcher_{"worker_0"};
  envoy::config::core::v3::Locality default_locality_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  DynamicModuleLbConfigSharedPtr lb_config_;
  std::unique_ptr<DynamicModuleLoadBalancer> lb_;
};

// `commit` posts to the worker dispatcher cached at construction; running the captured callback
// dispatches into DynamicModuleLoadBalancer::onScheduled without crashing.
TEST_F(DynamicModuleLoadBalancerSchedulerTest, CommitPostsToWorkerDispatcher) {
  auto* scheduler =
      envoy_dynamic_module_callback_cluster_lb_scheduler_new(static_cast<void*>(lb_.get()));
  ASSERT_NE(nullptr, scheduler);

  Event::PostCb captured_cb;
  EXPECT_CALL(worker_dispatcher_, post(testing::_))
      .WillOnce(testing::Invoke([&](Event::PostCb cb) { captured_cb = std::move(cb); }));

  envoy_dynamic_module_callback_cluster_lb_scheduler_commit(scheduler, /*event_id=*/42);
  ASSERT_TRUE(captured_cb);
  captured_cb();

  envoy_dynamic_module_callback_cluster_lb_scheduler_delete(scheduler);
}

// After the LB has been destroyed, the scheduler's weak_ptr to the shared state cannot be
// locked (the LB held the only strong reference) and `commit` becomes a no-op (no post issued).
TEST_F(DynamicModuleLoadBalancerSchedulerTest, CommitAfterLbDestroyedIsNoOp) {
  auto* scheduler =
      envoy_dynamic_module_callback_cluster_lb_scheduler_new(static_cast<void*>(lb_.get()));
  ASSERT_NE(nullptr, scheduler);

  // Destroying the LB releases the only strong reference to the shared state.
  lb_.reset();

  EXPECT_CALL(worker_dispatcher_, post(testing::_)).Times(0);
  envoy_dynamic_module_callback_cluster_lb_scheduler_commit(scheduler, /*event_id=*/42);

  envoy_dynamic_module_callback_cluster_lb_scheduler_delete(scheduler);
}

// `commit` is safe to call from a foreign thread: the dispatcher is read via an atomic and the
// LB is reached via a weak_ptr to its shared state.
TEST_F(DynamicModuleLoadBalancerSchedulerTest, CommitFromForeignThreadPosts) {
  auto* scheduler =
      envoy_dynamic_module_callback_cluster_lb_scheduler_new(static_cast<void*>(lb_.get()));
  ASSERT_NE(nullptr, scheduler);

  std::atomic<int> posts{0};
  EXPECT_CALL(worker_dispatcher_, post(testing::_))
      .WillRepeatedly(testing::Invoke([&](Event::PostCb) { posts.fetch_add(1); }));

  std::thread t([scheduler]() {
    envoy_dynamic_module_callback_cluster_lb_scheduler_commit(scheduler, /*event_id=*/0);
    envoy_dynamic_module_callback_cluster_lb_scheduler_commit(scheduler, /*event_id=*/1);
  });
  t.join();

  EXPECT_EQ(2, posts.load());

  envoy_dynamic_module_callback_cluster_lb_scheduler_delete(scheduler);
}

} // namespace
} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
