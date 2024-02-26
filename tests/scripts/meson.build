common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
common_env.prepend('WIREPLUMBER_DATA_DIR', meson.current_source_dir())
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
common_args = [
  '-D_GNU_SOURCE',
]

script_tester = executable('script-tester',
    '..'/'script-tester.c',
    dependencies: common_deps, c_args: common_args
)

test(
  'test-linking-non-default-device-node',
  script_tester,
  args: ['script-tests', '00-test-linking-non-default-device-node.lua'],
  env: common_env,
)

test(
  'test-linking-non-default-device-node-capture',
  script_tester,
  args: ['script-tests', '01-test-linking-non-default-device-node-capture.lua'],
  env: common_env,
)

test(
  'test-linking-default-device-node',
  script_tester,
  args: ['script-tests', '02-test-linking-default-device-node.lua'],
  env: common_env,
)

test(
  'test-linking-default-device-node-capture',
  script_tester,
  args: ['script-tests', '03-test-linking-default-device-node-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-string',
  script_tester,
  args: ['script-tests', '04-test-linking-defined-device-node-target-object-string.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-int',
  script_tester,
  args: ['script-tests', '05-test-linking-defined-device-node-target-object-int.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-name',
  script_tester,
  args: ['script-tests', '06-test-linking-defined-device-node-target-object-name.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-string-capture',
  script_tester,
  args: ['script-tests', '07-test-linking-defined-device-node-target-object-string-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-int-capture',
  script_tester,
  args: ['script-tests', '08-test-linking-defined-device-node-target-object-int-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-name-capture',
  script_tester,
  args: ['script-tests', '09-test-linking-defined-device-node-target-object-name-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-node-target',
  script_tester,
  args: ['script-tests', '10-test-linking-defined-device-node-node-target.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-node-target-capture',
  script_tester,
  args: ['script-tests', '11-test-linking-defined-device-node-node-target-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-node-metadata',
  script_tester,
  args: ['script-tests', '12-test-linking-defined-device-node-target-node-metadata.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-node-metadata-capture',
  script_tester,
  args: ['script-tests', '13-test-linking-defined-device-node-target-node-metadata-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-metadata',
  script_tester,
  args: ['script-tests', '14-test-linking-defined-device-node-target-object-metadata.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-metadata-capture',
  script_tester,
  args: ['script-tests', '15-test-linking-defined-device-node-target-object-metadata-capture.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-int-metadata',
  script_tester,
  args: ['script-tests', '16-test-linking-defined-device-node-target-object-int-metadata.lua'],
  env: common_env,
)

test(
  'test-linking-defined-device-node-target-object-int-metadata-capture',
  script_tester,
  args: ['script-tests', '17-test-linking-defined-device-node-target-object-int-metadata-capture.lua'],
  env: common_env,
)


test(
  '00-test-default-nodes-initial-metadata-update',
  script_tester,
  args: ['script-tests', '00-test-default-nodes-initial-metadata-update.lua'],
  env: common_env,
)
