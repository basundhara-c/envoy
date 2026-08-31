Added :ref:`resource_limit_configs
<envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.resource_limit_configs>`,
an ``envoy.circuit_breakers`` extension point that lets a cluster substitute the built-in
per-dimension circuit-breaker counter with a custom resource limit (for example one that keys
admission by a request attribute). Supported for the connections, pending requests, requests, and
connection pools dimensions; the retries dimension is not supported.
