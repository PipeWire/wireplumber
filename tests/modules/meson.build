common_deps = [gobject_dep, gio_dep, wp_dep, pipewire_dep]
common_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
]

test(
  'test-algorithms',
  executable('test-algorithms',
    [
      'algorithms.c',
      '../../modules/module-pipewire/algorithms.c'
    ],
    dependencies: common_deps),
  env: common_env,
)
