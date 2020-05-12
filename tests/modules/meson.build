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
  'test-algorithms',
  executable('test-algorithms',
    [
      'algorithms.c',
      '../../modules/module-si-adapter/algorithms.c'
    ],
    dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-config-static-nodes',
  executable('test-config-static-nodes',
    [
      'config-static-nodes.c',
    ],
    dependencies: common_deps + [wptoml_dep], c_args: common_args),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-config-endpoint',
  executable('test-config-endpoint', 'config-endpoint.c',
    dependencies: common_deps + [wptoml_dep], c_args: common_args),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-config-policy',
  executable('test-config-policy',
    [
      'config-policy.c',
    ],
    dependencies: common_deps + [wptoml_dep], c_args: common_args),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-dbus-device-reservation',
  executable('test-dbus-device-reservation',
    [
      'dbus-device-reservation.c',
      '../../modules/module-monitor/dbus-device-reservation.c',
      reserve_device_interface_src,
    ],
    dependencies: common_deps + [giounix_dep], c_args: common_args),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-si-simple-node-endpoint',
  executable('test-si-simple-node-endpoint', 'si-simple-node-endpoint.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-audio-softdsp-endpoint',
  executable('test-si-audio-softdsp-endpoint', 'si-audio-softdsp-endpoint.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-standard-link',
  executable('test-si-standard-link', 'si-standard-link.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)
