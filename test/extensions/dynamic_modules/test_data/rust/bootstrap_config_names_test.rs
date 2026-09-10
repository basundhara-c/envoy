//! Test module for the bootstrap config-name accessors.
//!
//! On an admin request it enumerates the live config-object names via `config_names()`, buckets
//! them by kind, and performs the per-workload subset check a network-readiness reconcile does: a
//! workload is ready when every expected name is present in its kind's observed set. The observed
//! names and the readiness verdict are returned in the response body.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::BTreeSet;

declare_bootstrap_init_functions!(my_program_init, my_new_bootstrap_extension_config_fn);

fn my_program_init() -> bool {
  true
}

fn my_new_bootstrap_extension_config_fn(
  envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let registered = envoy_extension_config.register_admin_handler(
    "/config_names",
    "Dump live config-object names by kind.",
    true,
    false,
  );
  assert!(registered, "admin handler registration should succeed");
  envoy_extension_config.signal_init_complete();
  Some(Box::new(ConfigNamesTestConfig {}))
}

struct ConfigNamesTestConfig {}

impl BootstrapExtensionConfig for ConfigNamesTestConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy_extension: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(ConfigNamesTestExtension {})
  }

  fn on_admin_request(
    &self,
    envoy_extension_config: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    _path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    // Bucket the live config-object names by kind, exactly as a per-workload readiness reconcile
    // would consume them.
    let mut filter_chains = BTreeSet::new();
    let mut clusters = BTreeSet::new();
    let mut transport_socket_matches = BTreeSet::new();
    let mut secrets = BTreeSet::new();
    for (kind, name) in envoy_extension_config.config_names() {
      match kind {
        ConfigNameKind::FilterChain => filter_chains.insert(name),
        ConfigNameKind::Cluster => clusters.insert(name),
        ConfigNameKind::TransportSocketMatch => transport_socket_matches.insert(name),
        ConfigNameKind::Secret => secrets.insert(name),
      };
    }

    // A workload is ready when every expected name is observed in its kind's set (the subset check).
    let expected_clusters = ["cluster_0"];
    let expected_filter_chains = ["workload_chain"];
    let ready = expected_clusters.iter().all(|n| clusters.contains(*n))
      && expected_filter_chains.iter().all(|n| filter_chains.contains(*n));

    let join = |set: &BTreeSet<String>| set.iter().cloned().collect::<Vec<_>>().join(",");
    let body = format!(
      "clusters=[{}] filter_chains=[{}] transport_socket_matches=[{}] secrets=[{}] readiness={}",
      join(&clusters),
      join(&filter_chains),
      join(&transport_socket_matches),
      join(&secrets),
      if ready { "satisfied" } else { "unsatisfied" },
    );
    (200, body)
  }
}

struct ConfigNamesTestExtension {}

impl BootstrapExtension for ConfigNamesTestExtension {
  fn on_server_initialized(&mut self, _envoy_extension: &mut dyn EnvoyBootstrapExtension) {
    envoy_log_info!("Bootstrap config names test: server initialized");
  }
}
