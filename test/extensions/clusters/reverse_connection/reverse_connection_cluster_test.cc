#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/extensions/clusters/reverse_connection/reverse_connection.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection)
      : TestLoadBalancerContext(connection, nullptr) {}
  TestLoadBalancerContext(const Network::Connection* connection,
                          StreamInfo::StreamInfo* request_stream_info)
      : connection_(connection), request_stream_info_(request_stream_info) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : TestLoadBalancerContext(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext.
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return request_stream_info_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  StreamInfo::StreamInfo* request_stream_info_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class ReverseConnectionClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ReverseConnectionClusterTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Create the config.
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface.
    socket_interface_ =
        std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptor>(server_context_);

    // Create the extension.
    extension_ = std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, server_context_, config_);

    // Set up thread local slot.
    setupThreadLocalSlot();
  }

  ~ReverseConnectionClusterTest() override = default;

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    }
    setup(Upstream::parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    RevConClusterFactory factory;

    // Parse the RevConClusterConfig from the cluster's typed_config.
    envoy::extensions::clusters::reverse_connection::v3::RevConClusterConfig rev_con_config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), validation_visitor_, rev_con_config));

    auto status_or_pair =
        factory.createClusterWithConfig(cluster_config, rev_con_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_pair.status());

    cluster_ = std::dynamic_pointer_cast<RevConCluster>(status_or_pair.value().first);
    priority_update_cb_ = cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) {
          membership_updated_.ready();
          return absl::OkStatus();
        });
    ON_CALL(initialized_, ready()).WillByDefault(testing::Invoke([this] {
      init_complete_ = true;
    }));
    cluster_->initialize([&]() {
      initialized_.ready();
      return absl::OkStatus();
    });
  }

  void TearDown() override {
    if (init_complete_) {
      // EXPECT_CALL(server_context_.dispatcher_, post(_));
      EXPECT_CALL(*cleanup_timer_, disableTimer());
    }

    // Clean up thread local resources.
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // First, call onServerInitialized to set up the extension reference properly.
    extension_->onServerInitialized();

    // Create a thread local registry with the properly initialized extension.
    thread_local_registry_ =
        std::make_shared<BootstrapReverseConnection::UpstreamSocketThreadLocal>(
            server_context_.dispatcher_, extension_.get());

    // Create the actual TypedSlot.
    tls_slot_ =
        ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>::makeUnique(
            thread_local_);
    thread_local_.setDispatcher(&server_context_.dispatcher_);

    // Set up the slot to return our registry.
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version.
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<BootstrapReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered socket interface.
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  // Helper to add a socket to the manager for testing.
  void addTestSocket(const std::string& node_id, const std::string& cluster_id) {
    if (!thread_local_registry_ || !thread_local_registry_->socketManager()) {
      return;
    }

    // Set up mock expectations for timer and file event creation.
    auto mock_timer = new NiceMock<Event::MockTimer>();
    auto mock_file_event = new NiceMock<Event::MockFileEvent>();
    EXPECT_CALL(server_context_.dispatcher_, createTimer_(_)).WillOnce(Return(mock_timer));
    EXPECT_CALL(server_context_.dispatcher_, createFileEvent_(_, _, _, _))
        .WillOnce(Return(mock_file_event));

    // Create a mock socket.
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    socket->io_handle_ = std::move(mock_io_handle);

    // Get the socket manager from the thread local registry.
    auto* tls_socket_manager = socket_interface_->getLocalRegistry()->socketManager();
    EXPECT_NE(tls_socket_manager, nullptr);

    // Add the socket to the manager.
    tls_socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket),
                                            std::chrono::seconds(30), false);
  }

  // Helper method to call cleanup since this class is a friend of RevConCluster.
  void callCleanup() { cluster_->cleanup(); }

  // Helper method to create LoadBalancerFactory instance for testing.
  std::unique_ptr<RevConCluster::LoadBalancerFactory> createLoadBalancerFactory() {
    return std::make_unique<RevConCluster::LoadBalancerFactory>(cluster_);
  }

  // Helper method to create ThreadAwareLoadBalancer instance for testing.
  std::unique_ptr<RevConCluster::ThreadAwareLoadBalancer> createThreadAwareLoadBalancer() {
    return std::make_unique<RevConCluster::ThreadAwareLoadBalancer>(cluster_);
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  std::shared_ptr<RevConCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};

  // Real thread local slot and registry for reverse connection testing.
  std::unique_ptr<ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>>
      tls_slot_;
  std::shared_ptr<BootstrapReverseConnection::UpstreamSocketThreadLocal> thread_local_registry_;

  // Real socket interface and extension.
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptorExtension> extension_;

  // Mock thread local instance.
  NiceMock<ThreadLocal::MockInstance> thread_local_;

  // Mock dispatcher.
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  // Stats and config.
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
};

TEST(ReverseConnectionClusterConfigTest, GoodConfig) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        http_header_names:
          - x-remote-node-id
          - x-dst-cluster-uuid
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_TRUE(cluster_config.has_cluster_type());
  EXPECT_EQ(cluster_config.cluster_type().name(), "envoy.clusters.reverse_connection");
}

TEST(ReverseConnectionClusterConfigTest, CustomProxyHostSuffix) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        proxy_host_suffix: "custom.proxy.suffix"
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_TRUE(cluster_config.has_cluster_type());
  EXPECT_EQ(cluster_config.cluster_type().name(), "envoy.clusters.reverse_connection");
}

TEST_F(ReverseConnectionClusterTest, CustomProxyHostSuffixLogic) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        proxy_host_suffix: "custom.proxy.suffix"
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test that the custom proxy host suffix is used for Host header parsing.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuid.custom.proxy.suffix:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test that the default suffix is rejected.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuid.tcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }
}

TEST_F(ReverseConnectionClusterTest, BadConfigWithLoadAssignment) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    load_assignment:
      cluster_name: name
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "Reverse Conn clusters must have no load assignment configured");
}

TEST_F(ReverseConnectionClusterTest, BadConfigWithWrongLbPolicy) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "cluster: LB policy ROUND_ROBIN is not valid for Cluster type "
                            "envoy.clusters.reverse_connection. Only 'CLUSTER_PROVIDED' is allowed "
                            "with cluster type 'REVERSE_CONNECTION'");
}

TEST_F(ReverseConnectionClusterTest, BasicSetup) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        http_header_names:
          - x-remote-node-id
          - x-dst-cluster-uuid
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(ReverseConnectionClusterTest, NoContext) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test null context - should return nullptr.
  {
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(nullptr).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, NoHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Downstream connection but no headers => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, MissingRequiredHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Request with unsupported headers but missing all required headers (EnvoyDstNodeUUID,.
  // EnvoyDstClusterUUID, proper Host header).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-random-header", "random-value");
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test with empty header value - this should be skipped and continue to next header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-remote-node-id", "");
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, GetUUIDFromHostFunction) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test valid Host header format.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuid.tcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test valid Host header format with different UUID.
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "another-test-node-uuid.tcpproxy.envoy.remote:9090"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "another-test-node-uuid");
  }

  // Test Host header without port.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuid.tcpproxy.envoy.remote"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test invalid Host header - wrong suffix.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuid.wrong.suffix:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }

  // Test invalid Host header - no dot separator.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-node-uuidtcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }

  // Test invalid Host header - empty UUID.
  {
    auto headers = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", ".tcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_EQ(result.value(), "");
  }

  // Test invalid Host header - invalid port.
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "test-node-uuid.tcpproxy.envoy.remote:invalid"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }
}

TEST_F(ReverseConnectionClusterTest, GetUUIDFromSNIFunction) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test valid SNI format.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-node-uuid.tcpproxy.envoy.remote"));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test valid SNI format with different UUID.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("another-test-node123.tcpproxy.envoy.remote"));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "another-test-node123");
  }

  // Test empty SNI.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName()).WillRepeatedly(Return(""));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test null connection.
  {
    auto result = lb.getUUIDFromSNI(nullptr);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI with wrong suffix.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-node-uuid.wrong.suffix"));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI without suffix.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName()).WillRepeatedly(Return("test-node-uuid"));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI with empty UUID.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName()).WillRepeatedly(Return(".tcpproxy.envoy.remote"));

    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_EQ(result.value(), "");
  }
}

TEST_F(ReverseConnectionClusterTest, HostCreationWithSocketManager) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test host creation with Host header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }

  // Test host creation with SNI.
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-uuid-456.tcpproxy.envoy.remote"));

    TestLoadBalancerContext lb_context(&connection);
    // No Host header, so it should fall back to SNI.
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-456");
  }

  // Test host creation with HTTP headers.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-dst-cluster-uuid", "cluster-123");

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }
}

TEST_F(ReverseConnectionClusterTest, HostReuse) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Add test socket to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with same UUID - should reuse the same host.
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_EQ(result1.host, result2.host);
  }
}

TEST_F(ReverseConnectionClusterTest, DifferentHostsForDifferentUUIDs) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with different UUID - should be different host.
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-456.tcpproxy.envoy.remote:8080"}}};
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_NE(result1.host, result2.host);
  }
}

TEST_F(ReverseConnectionClusterTest, TestCleanup) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-456.tcpproxy.envoy.remote:8080"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Verify hosts are different.
  EXPECT_NE(host1, host2);

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that hosts can still be accessed after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, TestCleanupWithUsedHosts) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-456.tcpproxy.envoy.remote:8080"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Mark one host as used by acquiring a handle.
  auto handle1 = host1->acquireHandle();
  EXPECT_TRUE(host1->used());

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that the used host is still accessible after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }

  // Release the handle.
  handle1.reset();
}
TEST_F(ReverseConnectionClusterTest, LoadBalancerFactory) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Test LoadBalancerFactory using helper method.
  auto factory = createLoadBalancerFactory();
  EXPECT_NE(factory, nullptr);

  // Test that the factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);

  // Test that multiple load balancers are different instances.
  auto lb2 = factory->create(params);
  EXPECT_NE(lb2, nullptr);
  EXPECT_NE(lb.get(), lb2.get());

  // Test create() without parameters.
  auto lb3 = factory->create();
  EXPECT_NE(lb3, nullptr);
}

TEST_F(ReverseConnectionClusterTest, ThreadAwareLoadBalancer) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Test ThreadAwareLoadBalancer using helper method.
  auto thread_aware_lb = createThreadAwareLoadBalancer();
  EXPECT_NE(thread_aware_lb, nullptr);

  // Test initialize() method.
  auto init_status = thread_aware_lb->initialize();
  EXPECT_TRUE(init_status.ok());

  // Test factory() method.
  auto factory = thread_aware_lb->factory();
  EXPECT_NE(factory, nullptr);

  // Test that factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);
}

TEST_F(ReverseConnectionClusterTest, LoadBalancerUnsupportedMethods) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test peekAnotherHost - should return nullptr.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    Upstream::HostConstSharedPtr peeked_host = lb.peekAnotherHost(&lb_context);
    EXPECT_EQ(peeked_host, nullptr);
  }

  // Test selectExistingConnection - should return nullopt.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    std::vector<uint8_t> hash_key;

    // Create a mock host for testing.
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto selected_connection = lb.selectExistingConnection(&lb_context, *mock_host, hash_key);
    EXPECT_FALSE(selected_connection.has_value());
  }

  // Test lifetimeCallbacks - should return empty OptRef.
  {
    auto lifetime_callbacks = lb.lifetimeCallbacks();
    EXPECT_FALSE(lifetime_callbacks.has_value());
  }
}

class UpstreamReverseConnectionAddressTest : public testing::Test {
public:
  void SetUp() override {}

  void TearDown() override {}

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(UpstreamReverseConnectionAddressTest, BasicSetup) {
  const std::string node_id = "test-node-123";
  UpstreamReverseConnectionAddress address(node_id);

  // Test basic properties.
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.asStringView(), "127.0.0.1:0");
  EXPECT_EQ(address.logicalName(), node_id);
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
  EXPECT_EQ(address.addressType(), "default");
  EXPECT_FALSE(address.networkNamespace().has_value());
}

TEST_F(UpstreamReverseConnectionAddressTest, EqualityOperator) {
  UpstreamReverseConnectionAddress address1("node-1");
  UpstreamReverseConnectionAddress address2("node-1");
  UpstreamReverseConnectionAddress address3("node-2");

  // Same node ID should be equal.
  EXPECT_TRUE(address1 == address2);
  EXPECT_TRUE(address2 == address1);

  // Different node IDs should not be equal.
  EXPECT_FALSE(address1 == address3);
  EXPECT_FALSE(address3 == address1);

  // Test with different address types.
  Network::Address::Ipv4Instance ipv4_address("127.0.0.1", 8080);
  EXPECT_FALSE(address1 == ipv4_address);
}

TEST_F(UpstreamReverseConnectionAddressTest, SocketAddressMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test sockAddr and sockAddrLen.
  const sockaddr* sock_addr = address.sockAddr();
  EXPECT_NE(sock_addr, nullptr);

  socklen_t addr_len = address.sockAddrLen();
  EXPECT_EQ(addr_len, sizeof(struct sockaddr_in));

  // Verify the socket address structure.
  const struct sockaddr_in* addr_in = reinterpret_cast<const struct sockaddr_in*>(sock_addr);
  EXPECT_EQ(addr_in->sin_family, AF_INET);
  EXPECT_EQ(ntohs(addr_in->sin_port), 0);
  EXPECT_EQ(ntohl(addr_in->sin_addr.s_addr), 0x7f000001); // 127.0.0.1
}

TEST_F(UpstreamReverseConnectionAddressTest, IPMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test IP-related methods.
  const Network::Address::Ip* ip = address.ip();
  EXPECT_NE(ip, nullptr);

  // Test IP address properties.
  EXPECT_EQ(ip->addressAsString(), "0.0.0.0:0");
  EXPECT_TRUE(ip->isAnyAddress());
  EXPECT_FALSE(ip->isUnicastAddress());
  EXPECT_EQ(ip->port(), 0);
  EXPECT_EQ(ip->version(), Network::Address::IpVersion::v4);

  // Test additional IP methods.
  EXPECT_FALSE(ip->isLinkLocalAddress());
  EXPECT_FALSE(ip->isUniqueLocalAddress());
  EXPECT_FALSE(ip->isSiteLocalAddress());
  EXPECT_FALSE(ip->isTeredoAddress());

  // Test IPv4/IPv6 methods.
  EXPECT_EQ(ip->ipv4(), nullptr);
  EXPECT_EQ(ip->ipv6(), nullptr);
}

TEST_F(UpstreamReverseConnectionAddressTest, PipeAndInternalAddressMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test pipe and internal address methods.
  EXPECT_EQ(address.pipe(), nullptr);
  EXPECT_EQ(address.envoyInternalAddress(), nullptr);
}

TEST_F(UpstreamReverseConnectionAddressTest, SocketInterfaceWithAvailableInterface) {
  UpstreamReverseConnectionAddress address("test-node");
  const Network::SocketInterface& socket_interface = address.socketInterface();

  // Should return the upstream reverse connection socket interface.
  // The interface should not be null since it's registered via reverse_tunnel_acceptor.h.
  EXPECT_NE(&socket_interface, nullptr);

  // Verify that the returned interface is of type ReverseTunnelAcceptor.
  const auto* reverse_tunnel_acceptor =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(&socket_interface);
  EXPECT_NE(reverse_tunnel_acceptor, nullptr);
}

TEST_F(UpstreamReverseConnectionAddressTest, MultipleInstances) {
  UpstreamReverseConnectionAddress address1("node-1");
  UpstreamReverseConnectionAddress address2("node-2");

  // Test that different instances have different logical names.
  EXPECT_EQ(address1.logicalName(), "node-1");
  EXPECT_EQ(address2.logicalName(), "node-2");

  // Test that they are not equal.
  EXPECT_FALSE(address1 == address2);
}

TEST_F(UpstreamReverseConnectionAddressTest, EmptyNodeId) {
  UpstreamReverseConnectionAddress address("");

  // Test with empty node ID.
  EXPECT_EQ(address.logicalName(), "");
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
}

TEST_F(UpstreamReverseConnectionAddressTest, LongNodeId) {
  const std::string long_node_id =
      "very-long-node-id-that-might-be-used-in-production-environments";
  UpstreamReverseConnectionAddress address(long_node_id);

  // Test with long node ID.
  EXPECT_EQ(address.logicalName(), long_node_id);
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
