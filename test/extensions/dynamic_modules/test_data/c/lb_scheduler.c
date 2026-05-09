// Test module that exercises the per-worker cluster LB scheduler.
//
// On `on_lb_new` the module captures the lb_envoy_ptr and creates a scheduler bound to it.
// On `on_lb_choose_host` the module spawns a worker thread (once) that commits two events
// (id=0 and id=1) to the scheduler from a non-Envoy thread, then waits for both events to be
// observed via `on_lb_scheduled`. The first round-robin host index is bumped on every commit,
// so by the time the test issues a second `chooseHost` it should observe a different host than
// it would have without the cross-thread post.

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

typedef struct {
  // Created in on_lb_new on the worker thread; freed in on_lb_destroy on the worker thread.
  envoy_dynamic_module_type_cluster_lb_scheduler_module_ptr scheduler;
  // Background thread joined in on_lb_destroy.
  pthread_t commit_thread;
  bool commit_thread_started;
  // Observed events, in order. event_count tracks the number of observations.
  uint64_t observed_events[2];
  atomic_size_t event_count;
  // Worker-thread state for round-robin.
  size_t next_index;
} lb_state;

static int config_marker = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_lb_config_module_ptr envoy_dynamic_module_on_lb_config_new(
    envoy_dynamic_module_type_lb_config_envoy_ptr lb_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)lb_config_envoy_ptr;
  (void)name;
  (void)config;
  return &config_marker;
}

void envoy_dynamic_module_on_lb_config_destroy(
    envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_lb_module_ptr
envoy_dynamic_module_on_lb_new(envoy_dynamic_module_type_lb_config_module_ptr config_module_ptr,
                               envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr) {
  (void)config_module_ptr;
  lb_state* state = (lb_state*)calloc(1, sizeof(lb_state));
  if (state == NULL) {
    return NULL;
  }
  // Create the scheduler from the worker thread that owns this LB.
  state->scheduler = envoy_dynamic_module_callback_cluster_lb_scheduler_new(lb_envoy_ptr);
  return state;
}

static void* commit_two_events(void* arg) {
  lb_state* state = (lb_state*)arg;
  // Both commits race the worker dispatcher; the LB module's on_lb_scheduled will observe
  // them in dispatch order on the worker thread.
  envoy_dynamic_module_callback_cluster_lb_scheduler_commit(state->scheduler, /*event_id=*/0);
  envoy_dynamic_module_callback_cluster_lb_scheduler_commit(state->scheduler, /*event_id=*/1);
  return NULL;
}

bool envoy_dynamic_module_on_lb_choose_host(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr context_envoy_ptr, uint32_t* result_priority,
    uint32_t* result_index) {
  (void)context_envoy_ptr;
  lb_state* state = (lb_state*)lb_module_ptr;

  // Spawn the commit thread once per LB instance, on the first host selection.
  if (!state->commit_thread_started) {
    state->commit_thread_started = true;
    pthread_create(&state->commit_thread, NULL, commit_two_events, state);
  }

  size_t host_count = envoy_dynamic_module_callback_lb_get_healthy_hosts_count(lb_envoy_ptr, 0);
  if (host_count == 0) {
    return false;
  }
  size_t index = state->next_index % host_count;
  state->next_index++;
  *result_priority = 0;
  *result_index = (uint32_t)index;
  return true;
}

void envoy_dynamic_module_on_lb_on_host_membership_update(
    envoy_dynamic_module_type_lb_envoy_ptr lb_envoy_ptr,
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr, size_t num_hosts_added,
    size_t num_hosts_removed) {
  (void)lb_envoy_ptr;
  (void)lb_module_ptr;
  (void)num_hosts_added;
  (void)num_hosts_removed;
}

void envoy_dynamic_module_on_lb_scheduled(
    envoy_dynamic_module_type_lb_module_ptr lb_module_ptr, uint64_t event_id) {
  lb_state* state = (lb_state*)lb_module_ptr;
  // We expect only event_ids 0 and 1; cap recording at 2 so out-of-range events don't overflow.
  size_t i = atomic_fetch_add(&state->event_count, 1);
  if (i < sizeof(state->observed_events) / sizeof(state->observed_events[0])) {
    state->observed_events[i] = event_id;
  }
  // Bump the round-robin counter on the worker thread, demonstrating that arbitrary worker-only
  // state can be safely mutated from the scheduler hook.
  state->next_index++;
}

void envoy_dynamic_module_on_lb_destroy(envoy_dynamic_module_type_lb_module_ptr lb_module_ptr) {
  lb_state* state = (lb_state*)lb_module_ptr;
  if (state->commit_thread_started) {
    pthread_join(state->commit_thread, NULL);
  }
  if (state->scheduler != NULL) {
    envoy_dynamic_module_callback_cluster_lb_scheduler_delete(state->scheduler);
  }
  free(state);
}
