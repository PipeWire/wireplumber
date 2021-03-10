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
  'test-audio-utils',
  executable('test-audio-utils',
    [
      'audio-utils.c',
      '../../modules/module-si-adapter/audio-utils.c'
    ],
    dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-reserve-device',
  executable('test-reserve-device', 'reserve-device.c',
    dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-node',
  executable('test-si-node', 'si-node.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-adapter',
  executable('test-si-adapter', 'si-adapter.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-convert',
  executable('test-si-convert', 'si-convert.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)

test(
  'test-si-standard-link',
  executable('test-si-standard-link', 'si-standard-link.c',
      dependencies: common_deps, c_args: common_args),
  env: common_env,
)
