#include <filesystem>

#include "envoy/common/exception.h"
#include "envoy/extensions/circuit_breakers/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/resource_manager_factory.h"

#include "source/extensions/circuit_breakers/dynamic_modules/config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace CircuitBreakers {
namespace DynamicModules {
namespace {

using testing::NiceMock;

class DynamicModuleCircuitBreakerFactoryTest : public testing::Test {
public:
  DynamicModuleCircuitBreakerFactoryTest() {
    const std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("circuit_breaker_no_op", "c");
    const std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }

  Upstream::CircuitBreakerResourceParams params() {
    return Upstream::CircuitBreakerResourceParams{
        Upstream::CircuitBreakerResource::Requests, 1, runtime_,
        "circuit_breakers.test.default.max_requests", gauge_, gauge_};
  }

  DynamicModuleCircuitBreakerFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Stats::MockGauge> gauge_;
};

TEST_F(DynamicModuleCircuitBreakerFactoryTest, FactoryName) {
  EXPECT_EQ(DynamicModuleCircuitBreakerName, factory_.name());
  EXPECT_EQ("envoy.circuit_breakers.dynamic_modules", factory_.name());
}

TEST_F(DynamicModuleCircuitBreakerFactoryTest, FactoryRegistration) {
  auto* factory = Registry::FactoryRegistry<Upstream::CircuitBreakerFactory>::getFactory(
      "envoy.circuit_breakers.dynamic_modules");
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ("envoy.circuit_breakers.dynamic_modules", factory->name());
}

TEST_F(DynamicModuleCircuitBreakerFactoryTest, Category) {
  EXPECT_EQ("envoy.circuit_breakers", factory_.category());
}

// Happy path: a module that loads, resolves all symbols, and returns non-null from config_new and
// new yields a non-null ResourceLimit.
TEST_F(DynamicModuleCircuitBreakerFactoryTest, ValidConfigNameBased) {
  const std::string yaml = R"EOF(
dynamic_module_config:
  name: circuit_breaker_no_op
  do_not_close: true
breaker_name: test_breaker
)EOF";
  envoy::extensions::circuit_breakers::dynamic_modules::v3::DynamicModuleCircuitBreaker proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);

  auto limit = factory_.createResourceLimit(proto_config, params(), context_);
  EXPECT_NE(nullptr, limit);
}

// A module that cannot be found fails to load and throws.
TEST_F(DynamicModuleCircuitBreakerFactoryTest, BadModuleName) {
  envoy::extensions::circuit_breakers::dynamic_modules::v3::DynamicModuleCircuitBreaker proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("does_not_exist");
  proto_config.set_breaker_name("test_breaker");

  EXPECT_THROW(factory_.createResourceLimit(proto_config, params(), context_), EnvoyException);
}

// A module missing a required symbol (config_new) is rejected during config creation.
TEST_F(DynamicModuleCircuitBreakerFactoryTest, MissingRequiredSymbol) {
  envoy::extensions::circuit_breakers::dynamic_modules::v3::DynamicModuleCircuitBreaker proto_config;
  proto_config.mutable_dynamic_module_config()->set_name("circuit_breaker_missing_config_new");
  proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
  proto_config.set_breaker_name("test_breaker");

  EXPECT_THROW(factory_.createResourceLimit(proto_config, params(), context_), EnvoyException);
}

TEST_F(DynamicModuleCircuitBreakerFactoryTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  ASSERT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::circuit_breakers::dynamic_modules::v3::
                                     DynamicModuleCircuitBreaker*>(proto.get()));
}

} // namespace
} // namespace DynamicModules
} // namespace CircuitBreakers
} // namespace Extensions
} // namespace Envoy
