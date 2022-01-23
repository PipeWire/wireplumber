common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
common_env.set('WIREPLUMBER_DATA_DIR', meson.current_source_dir())

test(
  'test-wplua',
  executable('test-wplua', 'wplua.c', dependencies: common_deps),
  env: common_env,
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
)
test(
  'test-lua-json',
  script_tester,
  args: ['json.lua'],
  env: common_env,
)
test(
  'test-lua-monitor-rules',
  script_tester,
  args: ['monitor-rules.lua'],
  env: common_env,
)
