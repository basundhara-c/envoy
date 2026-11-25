An in-place listener update now releases the previous listener version on the next event loop when
it has no filter chains or addresses to drain, instead of holding it for the drain time. This
behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.listener_release_empty_drain_group`` to ``false``.
