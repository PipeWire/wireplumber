common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
common_env.prepend('WIREPLUMBER_DATA_DIR', meson.current_source_dir())
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
common_args = [
  '-D_GNU_SOURCE',
]

test(
  'test-wplua',
  executable('test-wplua', 'wplua.c',
    dependencies: common_deps, c_args: common_args),
  env: common_env,
)

script_tester = executable('script-tester',
    '..'/'script-tester.c',
    dependencies: common_deps, c_args: common_args
)

test(
  'test-lua-pod',
  script_tester,
  args: ['lua-api-tests', 'pod.lua'],
  env: common_env,
)
test(
  'test-lua-json',
  script_tester,
  args: ['lua-api-tests', 'json.lua'],
  env: common_env,
)
test(
  'test-lua-json-utils',
  script_tester,
  args: ['lua-api-tests', 'json-utils.lua'],
  env: common_env,
)
test(
  'test-lua-monitor-rules',
  script_tester,
  args: ['lua-api-tests', 'monitor-rules.lua'],
  env: common_env,
)
test(
  'test-lua-require',
  script_tester,
  args: ['lua-api-tests', 'require.lua'],
  env: common_env,
)
test(
  'test-lua-async-activation',
  script_tester,
  args: ['lua-api-tests', 'async-activation.lua'],
  env: common_env,
)
test(
  'test-lua-settings',
  script_tester,
  args: ['lua-api-tests', 'settings.lua'],
  env: common_env,
)
test(
  'test-lua-event-hooks',
  script_tester,
  args: ['lua-api-tests', 'event-hooks.lua'],
  env: common_env,
)
