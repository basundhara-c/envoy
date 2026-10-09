Added the ``envoy.reloadable_features.incremental_health_transition`` runtime guard, off by
default. When enabled, a host health change re-partitions only the priority that contains the
host instead of every priority in the cluster.
