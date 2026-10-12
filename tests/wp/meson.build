common_deps = [gobject_dep, gio_dep, wp_dep, pipewire_dep]
common_env = common_test_env
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

test(
  'wp/component-loader',
  executable('test-component-loader', 'component-loader.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/core',
  executable('test-core', 'core.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/events',
  executable('test-events', 'events.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/client-context',
  executable('test-client-context', 'client-context.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/metadata',
  executable('test-metadata', 'metadata.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/object-interest',
  executable('test-object-interest', 'object-interest.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/object-manager',
  executable('test-object-manager', 'object-manager.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/properties',
  executable('test-properties', 'properties.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/proxy',
  executable('test-proxy', 'proxy.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/session-item',
  executable('test-session-item', 'session-item.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/spa-json',
  executable('test-spa-json', 'spa-json.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/spa-pod',
  executable('test-spa-pod', 'spa-pod.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/spa-type',
  executable('test-spa-type', 'spa-type.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/state',
  executable('test-state', 'state.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/settings',
  executable('test-settings', 'settings.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/transition',
  executable('test-transition', 'transition.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/factory',
  executable('test-factory', 'factory.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/conf',
  executable('test-conf', 'conf.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/json-utils',
  executable('test-json-utils', 'json-utils.c',
      dependencies: common_deps),
  env: common_env,
)

test(
  'wp/dynamic-rules',
  executable('test-dynamic-rules', 'dynamic-rules.c',
      dependencies: common_deps),
  env: common_env,
)
