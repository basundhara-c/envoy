#include <functional>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace {

// Drives the per-tenant circuit breaker dynamic module end to end. The cluster's requests dimension
// is backed by the module with a per-tenant max of 1, keyed by the x-tenant-id header. All tenant
// bookkeeping lives inside the module; the core carries only the opaque token.
class DynamicModuleCircuitBreakerIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModuleCircuitBreakerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void setUpTestModulePath() {
    const std::string shared_object_path = Extensions::DynamicModules::testSharedObjectPath(
        "circuit_breaker_per_tenant_integration_test", "rust");
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  void initializeWithPerTenantBreaker() {
    setUpTestModulePath();
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      auto* thresholds = cluster->mutable_circuit_breakers()->add_thresholds();
      // The module reads the configured max as the per-tenant limit.
      thresholds->mutable_max_requests()->set_value(1);
      auto* resource_limit = thresholds->add_resource_limit_configs();
      resource_limit->set_name("envoy.circuit_breakers.dynamic_modules");
      const std::string typed_config = R"EOF(
"@type": type.googleapis.com/envoy.extensions.circuit_breakers.dynamic_modules.v3.DynamicModuleCircuitBreaker
dynamic_module_config:
  name: circuit_breaker_per_tenant_integration_test
  do_not_close: true
breaker_name: per_tenant
)EOF";
      TestUtility::loadFromYaml(typed_config, *resource_limit->mutable_typed_config());
    });
    initialize();
  }

  // Headers whose tenant is carried in the x-tenant-id header.
  Http::TestRequestHeaderMapImpl headersForTenant(absl::string_view tenant) {
    auto headers = default_request_headers_;
    headers.setCopy(Http::LowerCaseString("x-tenant-id"), tenant);
    return headers;
  }

  // Headers whose tenant is embedded in the path using the module's /tenants/<tenant>/... format.
  // No x-tenant-id header, so the module must fall back to parsing the path.
  Http::TestRequestHeaderMapImpl headersForPathTenant(absl::string_view tenant) {
    auto headers = default_request_headers_;
    headers.setPath(absl::StrCat("/tenants/", tenant, "/data"));
    return headers;
  }

  // Drives the per-tenant isolation scenario: two concurrent tenant-A requests (2nd overflows) and
  // one tenant-B request (admitted). `make_headers` builds the request headers for a tenant name,
  // letting the same choreography exercise both header-keyed and path-keyed tenant resolution.
  void runPerTenantIsolationScenario(
      const std::function<Http::TestRequestHeaderMapImpl(absl::string_view)>& make_headers) {
    // Each concurrent request uses its own downstream HTTP/1 connection so they can be in flight
    // simultaneously (a single HTTP/1 codec client pipelines requests).
    auto client_a1 = makeHttpConnection(lookupPort("http"));
    auto client_a2 = makeHttpConnection(lookupPort("http"));
    auto client_b1 = makeHttpConnection(lookupPort("http"));

    // First tenant-A request is admitted and held open at the upstream (occupies A's single slot).
    // Each admitted request opens its own upstream HTTP/1 connection, so accept them explicitly.
    auto response_a1 = client_a1->makeHeaderOnlyRequest(make_headers("tenant-a"));
    FakeHttpConnectionPtr upstream_conn_a1;
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_conn_a1));
    FakeStreamPtr upstream_a1;
    ASSERT_TRUE(upstream_conn_a1->waitForNewStream(*dispatcher_, upstream_a1));
    ASSERT_TRUE(upstream_a1->waitForEndStream(*dispatcher_));

    // Second tenant-A request overflows A's per-tenant limit and is rejected locally with a 503.
    auto response_a2 = client_a2->makeHeaderOnlyRequest(make_headers("tenant-a"));
    ASSERT_TRUE(response_a2->waitForEndStream());
    EXPECT_EQ("503", response_a2->headers().getStatusValue());
    test_server_->waitForCounter("cluster.cluster_0.upstream_rq_active_overflow", 1);

    // A tenant-B request uses B's own slot and is admitted (reaches the upstream).
    auto response_b1 = client_b1->makeHeaderOnlyRequest(make_headers("tenant-b"));
    FakeHttpConnectionPtr upstream_conn_b1;
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_conn_b1));
    FakeStreamPtr upstream_b1;
    ASSERT_TRUE(upstream_conn_b1->waitForNewStream(*dispatcher_, upstream_b1));
    ASSERT_TRUE(upstream_b1->waitForEndStream(*dispatcher_));
    upstream_b1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response_b1->waitForEndStream());
    EXPECT_EQ("200", response_b1->headers().getStatusValue());

    // Finish tenant A's first request.
    upstream_a1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response_a1->waitForEndStream());
    EXPECT_EQ("200", response_a1->headers().getStatusValue());

    ASSERT_TRUE(upstream_conn_a1->close());
    ASSERT_TRUE(upstream_conn_b1->close());
    client_a1->close();
    client_a2->close();
    client_b1->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModuleCircuitBreakerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Tenant A's second concurrent request overflows its own per-tenant limit of 1, while tenant B is
// unaffected. This proves per-tenant isolation with no per-tenant state in core, with the tenant
// carried in the x-tenant-id header.
TEST_P(DynamicModuleCircuitBreakerIntegrationTest, PerTenantIsolation) {
  initializeWithPerTenantBreaker();
  runPerTenantIsolationScenario([this](absl::string_view t) { return headersForTenant(t); });
}

// Same per-tenant isolation, but the tenant is embedded in the request path
// (/tenants/<tenant>/data) with no x-tenant-id header. The module extracts the tenant from the
// path, proving path-parameter-based enforcement works through the same context seam.
TEST_P(DynamicModuleCircuitBreakerIntegrationTest, PerTenantIsolationFromPath) {
  initializeWithPerTenantBreaker();
  runPerTenantIsolationScenario([this](absl::string_view t) { return headersForPathTenant(t); });
}

} // namespace
} // namespace Envoy
