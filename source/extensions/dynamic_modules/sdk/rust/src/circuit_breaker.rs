//! Circuit breaker support for dynamic modules.
//!
//! A circuit breaker provides a custom `ResourceLimit` for one cluster resource dimension. Unlike
//! the built-in cluster-wide counter, a module can key its bookkeeping by a request attribute read
//! from the per-request context, enabling limits such as "at most N in flight per tenant".
//!
//! Register a factory through the `circuit_breaker:` arm of [`crate::declare_all_init_functions!`]
//! and return a [`CircuitBreakerConfig`] from it.
//!
//! # Threading
//!
//! A single [`CircuitBreaker`] instance is shared across all worker threads for the life of the
//! cluster. [`CircuitBreaker::can_create`], [`CircuitBreaker::inc`], and [`CircuitBreaker::dec`] are
//! called concurrently from any worker thread, so the trait requires `Send + Sync` and
//! implementations must synchronize their own state. This is a stronger contract than the other,
//! thread-local, dynamic-module extension instances.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use crate::abi;

/// Per-request context for a single admission decision, passed to [`CircuitBreaker::can_create`]
/// and [`CircuitBreaker::inc`]. Backed by an opaque Envoy pointer that is valid only for the
/// duration of the hook; do not retain it.
pub struct EnvoyCircuitBreakerContext {
    raw_ptr: abi::envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr,
}

impl EnvoyCircuitBreakerContext {
    /// Wraps the opaque context handle passed by Envoy. Used internally by the SDK.
    #[doc(hidden)]
    pub fn new(raw_ptr: abi::envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr) -> Self {
        Self { raw_ptr }
    }

    /// Reads a single downstream request header value, returning `None` if the header is absent or
    /// no request context is available. The returned bytes are copied out, so they are safe to keep
    /// after the hook returns.
    pub fn request_header(&self, key: &str) -> Option<Vec<u8>> {
        if self.raw_ptr.is_null() {
            return None;
        }
        let key_buf = abi::envoy_dynamic_module_type_module_buffer {
            ptr: key.as_ptr() as *const _,
            length: key.len(),
        };
        let mut result = abi::envoy_dynamic_module_type_envoy_buffer {
            ptr: ptr::null(),
            length: 0,
        };
        // SAFETY: `raw_ptr` is a live context for the duration of the hook, `key_buf` points at the
        // caller's `key`, and `result` is a valid out-parameter.
        let found = unsafe {
            abi::envoy_dynamic_module_callback_circuit_breaker_get_request_header(
                self.raw_ptr,
                key_buf,
                &mut result,
            )
        };
        if !found || result.ptr.is_null() {
            return None;
        }
        // SAFETY: on success Envoy populated `(ptr, length)` describing a live buffer valid until
        // the hook returns; copy it out immediately.
        let slice = unsafe { std::slice::from_raw_parts(result.ptr as *const u8, result.length) };
        Some(slice.to_vec())
    }
}

/// Factory for circuit breaker instances. Created once per cluster+dimension on the main thread.
pub trait CircuitBreakerConfig: Send + Sync {
    /// Creates a circuit breaker instance for a resource dimension with the given configured
    /// maximum. Returning `None` declines the dimension, so Envoy keeps the built-in limit.
    fn new_circuit_breaker(&self, max: u64) -> Option<Box<dyn CircuitBreaker>>;
}

/// A circuit breaker instance for one resource dimension, shared across all worker threads.
pub trait CircuitBreaker: Send + Sync {
    /// Returns whether a new resource may be created for the request described by `context`.
    fn can_create(&self, context: &EnvoyCircuitBreakerContext) -> bool;

    /// Reserves one resource and returns an opaque token identifying the reservation. The token is
    /// passed back to [`CircuitBreaker::dec`] on release. Return 0 to mark the reservation as
    /// untracked (for example when no key could be extracted).
    fn inc(&self, context: &EnvoyCircuitBreakerContext) -> u64;

    /// Releases the resource previously reserved by [`CircuitBreaker::inc`], identified by `token`.
    /// A token of 0 must be a no-op, and an unknown or stale token must be safely ignored.
    fn dec(&self, token: u64);
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_config_new(
    _config_envoy_ptr: abi::envoy_dynamic_module_type_circuit_breaker_config_envoy_ptr,
    name: abi::envoy_dynamic_module_type_envoy_buffer,
    config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_circuit_breaker_config_module_ptr {
    catch_unwind(AssertUnwindSafe(|| {
        let name_str =
            unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
        let config_bytes = unsafe {
            crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
        };
        let new_fn = crate::NEW_CIRCUIT_BREAKER_CONFIG_FUNCTION
            .get()
            .expect("NEW_CIRCUIT_BREAKER_CONFIG_FUNCTION must be set");
        match new_fn(name_str.as_ref(), config_bytes) {
            Some(config) => crate::wrap_into_c_void_ptr!(config),
            None => ptr::null(),
        }
    }))
    .unwrap_or_else(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_config_new", panic);
        ptr::null()
    })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_config_destroy(
    config_ptr: abi::envoy_dynamic_module_type_circuit_breaker_config_module_ptr,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        crate::drop_wrapped_c_void_ptr!(config_ptr, CircuitBreakerConfig);
    }))
    .map_err(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_config_destroy", panic);
    });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_new(
    config_ptr: abi::envoy_dynamic_module_type_circuit_breaker_config_module_ptr,
    max: u64,
) -> abi::envoy_dynamic_module_type_circuit_breaker_module_ptr {
    catch_unwind(AssertUnwindSafe(|| {
        let config = &*(config_ptr as *const Box<dyn CircuitBreakerConfig>);
        match config.new_circuit_breaker(max) {
            Some(breaker) => crate::wrap_into_c_void_ptr!(breaker),
            None => ptr::null(),
        }
    }))
    .unwrap_or_else(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_new", panic);
        ptr::null()
    })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_can_create(
    cb_ptr: abi::envoy_dynamic_module_type_circuit_breaker_module_ptr,
    context_envoy_ptr: abi::envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let breaker = &*(cb_ptr as *const Box<dyn CircuitBreaker>);
        let context = EnvoyCircuitBreakerContext::new(context_envoy_ptr);
        breaker.can_create(&context)
    }))
    .unwrap_or_else(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_can_create", panic);
        // Fail open on a module panic so a broken breaker does not wedge all traffic.
        true
    })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_inc(
    cb_ptr: abi::envoy_dynamic_module_type_circuit_breaker_module_ptr,
    context_envoy_ptr: abi::envoy_dynamic_module_type_circuit_breaker_context_envoy_ptr,
) -> u64 {
    catch_unwind(AssertUnwindSafe(|| {
        let breaker = &*(cb_ptr as *const Box<dyn CircuitBreaker>);
        let context = EnvoyCircuitBreakerContext::new(context_envoy_ptr);
        breaker.inc(&context)
    }))
    .unwrap_or_else(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_inc", panic);
        0
    })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_dec(
    cb_ptr: abi::envoy_dynamic_module_type_circuit_breaker_module_ptr,
    token: u64,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        let breaker = &*(cb_ptr as *const Box<dyn CircuitBreaker>);
        breaker.dec(token);
    }))
    .map_err(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_dec", panic);
    });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_circuit_breaker_destroy(
    cb_ptr: abi::envoy_dynamic_module_type_circuit_breaker_module_ptr,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        crate::drop_wrapped_c_void_ptr!(cb_ptr, CircuitBreaker);
    }))
    .map_err(|panic| {
        crate::log_ffi_panic("envoy_dynamic_module_on_circuit_breaker_destroy", panic);
    });
}
