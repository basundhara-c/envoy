//! Scrappy per-tenant circuit breaker dynamic module for integration testing.
//!
//! This module keeps ALL tenant bookkeeping inside the module: a map of `tenant -> in-flight count`
//! and a map of `token -> tenant` so the context-free `dec` hook can find the right bucket. The
//! tenant key is resolved from the per-request context, preferring the `x-tenant-id` header and
//! falling back to a segment parsed out of the request path; requests with no resolvable tenant are
//! untracked (fail open). It demonstrates the opaque-token contract: `inc` returns a unique token,
//! `dec` looks it up and decrements, and an unknown/stale token is ignored.
//!
//! Path tenant format (a made-up convention for the test): the tenant id is the path segment that
//! immediately follows a literal `tenants` segment, i.e. `/tenants/<tenant>/...`. For example
//! `/tenants/acme/data` resolves to tenant `acme`. Anything else falls through to untracked.
//!
//! The C++ integration test drives two tenants through a cluster whose max_requests-per-tenant is 1
//! and asserts tenant A trips its own limit while tenant B is unaffected, for both the header-keyed
//! and path-keyed cases.

use envoy_proxy_dynamic_modules_rust_sdk::circuit_breaker::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

declare_all_init_functions!(
  init,
  circuit_breaker: new_circuit_breaker_config_fn,
);

fn init() -> bool {
  true
}

fn new_circuit_breaker_config_fn(
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn CircuitBreakerConfig>> {
  Some(Box::new(PerTenantConfig {}))
}

struct PerTenantConfig {}

impl CircuitBreakerConfig for PerTenantConfig {
  fn new_circuit_breaker(&self, max: u64) -> Option<Box<dyn CircuitBreaker>> {
    Some(Box::new(PerTenantBreaker::new(max)))
  }
}

/// The header carrying the tenant identity. Product-neutral on purpose.
const TENANT_HEADER: &str = "x-tenant-id";

/// The path segment that precedes the tenant id in the made-up `/tenants/<tenant>/...` format.
const TENANT_PATH_MARKER: &str = "tenants";

struct PerTenantBreaker {
  max: u64,
  // Interior mutability behind a Mutex because the instance is shared across all worker threads.
  state: Mutex<State>,
  next_token: AtomicU64,
}

#[derive(Default)]
struct State {
  // tenant -> current in-flight count.
  counts: HashMap<String, u64>,
  // token -> tenant, so the context-free dec() can find the bucket that inc() charged.
  tokens: HashMap<u64, String>,
}

impl PerTenantBreaker {
  fn new(max: u64) -> Self {
    Self {
      max,
      state: Mutex::new(State::default()),
      // Start tokens at 1 so 0 stays reserved as the untracked sentinel.
      next_token: AtomicU64::new(1),
    }
  }

  fn tenant_of(context: &EnvoyCircuitBreakerContext) -> Option<String> {
    // Prefer the explicit header, then fall back to the tenant embedded in the path.
    if let Some(bytes) = context.request_header(TENANT_HEADER) {
      return Some(String::from_utf8_lossy(&bytes).into_owned());
    }
    Self::tenant_from_path(context)
  }

  /// Extracts the tenant from the request path using the `/tenants/<tenant>/...` convention: the
  /// segment immediately after a `tenants` segment. The query string (after `?`) is ignored.
  fn tenant_from_path(context: &EnvoyCircuitBreakerContext) -> Option<String> {
    let bytes = context.request_header(":path")?;
    let path = String::from_utf8_lossy(&bytes);
    let path = path.split(['?', '#']).next().unwrap_or("");
    let segments: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
    let marker = segments.iter().position(|s| *s == TENANT_PATH_MARKER)?;
    segments.get(marker + 1).map(|s| s.to_string())
  }
}

impl CircuitBreaker for PerTenantBreaker {
  fn can_create(&self, context: &EnvoyCircuitBreakerContext) -> bool {
    // No tenant key: untracked, always admit.
    let Some(tenant) = Self::tenant_of(context) else {
      return true;
    };
    let state = self.state.lock().unwrap();
    let current = state.counts.get(&tenant).copied().unwrap_or(0);
    current < self.max
  }

  fn inc(&self, context: &EnvoyCircuitBreakerContext) -> u64 {
    // No tenant key: untracked, return the 0 sentinel so dec() is a no-op.
    let Some(tenant) = Self::tenant_of(context) else {
      return 0;
    };
    let token = self.next_token.fetch_add(1, Ordering::Relaxed);
    let mut state = self.state.lock().unwrap();
    *state.counts.entry(tenant.clone()).or_insert(0) += 1;
    state.tokens.insert(token, tenant);
    token
  }

  fn dec(&self, token: u64) {
    // Token 0 is untracked; unknown/stale tokens are ignored.
    if token == 0 {
      return;
    }
    let mut state = self.state.lock().unwrap();
    let Some(tenant) = state.tokens.remove(&token) else {
      return;
    };
    if let Some(count) = state.counts.get_mut(&tenant) {
      if *count > 0 {
        *count -= 1;
      }
    }
  }
}
