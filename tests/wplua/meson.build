common_deps = [wplua_dep]
common_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'WIREPLUMBER_DEBUG=7',
]

test(
  'test-wplua',
  executable('test-toml', 'wplua.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)
