common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
common_env.prepend('WIREPLUMBER_DATA_DIR', meson.current_source_dir())
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

test(
  'wplua/wplua',
  executable('test-wplua', 'wplua.c',
    dependencies: common_deps),
  env: common_env,
)

script_tester = executable('script-tester',
    '..'/'script-tester.c',
    dependencies: common_deps
)

test(
  'wplua/lua-pod',
  script_tester,
  args: ['lua-api-tests', 'pod.lua'],
  env: common_env,
)
test(
  'wplua/lua-json',
  script_tester,
  args: ['lua-api-tests', 'json.lua'],
  env: common_env,
)
test(
  'wplua/lua-json-utils',
  script_tester,
  args: ['lua-api-tests', 'json-utils.lua'],
  env: common_env,
)
test(
  'wplua/lua-monitor-rules',
  script_tester,
  args: ['lua-api-tests', 'monitor-rules.lua'],
  env: common_env,
)
test(
  'wplua/lua-require',
  script_tester,
  args: ['lua-api-tests', 'require.lua'],
  env: common_env,
)
test(
  'wplua/lua-async-activation',
  script_tester,
  args: ['lua-api-tests', 'async-activation.lua'],
  env: common_env,
)
test(
  'wplua/lua-settings',
  script_tester,
  args: ['lua-api-tests', 'settings.lua'],
  env: common_env,
)
test(
  'wplua/lua-event-hooks',
  script_tester,
  args: ['lua-api-tests', 'event-hooks.lua'],
  env: common_env,
)
test(
  'wplua/lua-properties',
  script_tester,
  args: ['lua-api-tests', 'properties.lua'],
  env: common_env,
)
test(
  'wplua/lua-node-utils',
  script_tester,
  args: ['lua-api-tests', 'node-utils.lua'],
  env: common_env,
)
test(
  'wplua/lua-profile-utils',
  script_tester,
  args: ['lua-api-tests', 'profile-utils.lua'],
  env: common_env,
)
