common_deps = [gobject_dep, gio_dep, wp_dep, pipewire_dep]
common_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'WIREPLUMBER_MODULE_DIR=@0@'.format(meson.current_build_dir() / '..' / '..' / 'modules'),
  'WIREPLUMBER_DEBUG=7',
]
common_args = [
  '-DG_LOG_USE_STRUCTURED',
]

test(
  'test-core',
  executable('test-core', 'core.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-endpoint',
  executable('test-endpoint', 'endpoint.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-metadata',
  executable('test-metadata', 'metadata.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)
test(
  'test-object-interest',
  executable('test-object-interest', 'object-interest.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-properties',
  executable('test-properties', 'properties.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-props',
  executable('test-props', 'props.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-proxy',
  executable('test-proxy', 'proxy.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-session',
  executable('test-session', 'session.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-session-item',
  executable('test-session-item', 'session-item.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-spa-pod',
  executable('test-spa-pod', 'spa-pod.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-spa-type',
  executable('test-spa-type', 'spa-type.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-transition',
  executable('test-transition', 'transition.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)
