common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'WIREPLUMBER_DATA_DIR=@0@'.format(meson.current_source_dir()),
  'WIREPLUMBER_MODULE_DIR=@0@'.format(meson.current_build_dir() / '..' / '..' / 'modules'),
  'WIREPLUMBER_DEBUG=7',
]

test(
  'test-wplua',
  executable('test-wplua', 'wplua.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)

script_tester = executable('script-tester',
    'script-tester.c',
    dependencies: common_deps
)

test(
  'test-lua-pod',
  script_tester,
  args: ['pod.lua'],
  env: common_env,
  workdir : meson.current_source_dir(),
)
