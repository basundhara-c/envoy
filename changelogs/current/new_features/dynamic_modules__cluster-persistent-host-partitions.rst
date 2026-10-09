dynamic modules: dynamic module clusters now opt in to persistent host partitions, so adding,
removing, or changing the health of hosts updates only those hosts. This takes effect only when
``envoy.reloadable_features.persistent_host_partitions`` is enabled.
