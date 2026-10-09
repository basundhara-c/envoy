Added the ``envoy.reloadable_features.persistent_host_partitions`` runtime guard, off by default.
When enabled, a cluster that opts in keeps its host partitions (all, healthy, degraded, excluded,
and each per locality) in persistent maps patched by membership and health deltas, instead of
rebuilding them on every host update. Worker threads adopt a snapshot of those partitions rather
than copying them. Clusters that do not opt in are unchanged.
