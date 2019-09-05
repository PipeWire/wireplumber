common_deps = [gobject_dep, wptoml_dep]
common_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
]

test(
  'test-wptoml',
  executable('test-toml', 'wptoml.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)
