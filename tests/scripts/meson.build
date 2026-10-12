common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
common_env.prepend('WIREPLUMBER_DATA_DIR', meson.current_source_dir())
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

script_tester = executable('script-tester',
    '..'/'script-tester.c',
    dependencies: common_deps
)

test(
  'scripts/linking/non-default-device-node',
  script_tester,
  args: ['script-tests', 'linking/non-default-device-node.lua'],
  env: common_env,
)

test(
  'scripts/linking/non-default-device-node-capture',
  script_tester,
  args: ['script-tests', 'linking/non-default-device-node-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/default-device-node',
  script_tester,
  args: ['script-tests', 'linking/default-device-node.lua'],
  env: common_env,
)

test(
  'scripts/linking/default-device-node-capture',
  script_tester,
  args: ['script-tests', 'linking/default-device-node-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-string',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-string.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-int',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-int.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-name',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-name.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-string-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-string-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-int-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-int-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-name-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-name-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-node-target',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-node-target.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-node-target-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-node-target-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-node-metadata',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-node-metadata.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-node-metadata-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-node-metadata-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-metadata',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-metadata.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-metadata-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-metadata-capture.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-int-metadata',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-int-metadata.lua'],
  env: common_env,
)

test(
  'scripts/linking/defined-device-node-target-object-int-metadata-capture',
  script_tester,
  args: ['script-tests', 'linking/defined-device-node-target-object-int-metadata-capture.lua'],
  env: common_env,
)


test(
  'scripts/default-nodes/initial-metadata-update',
  script_tester,
  args: ['script-tests', 'default-nodes/initial-metadata-update.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-idle-source',
  script_tester,
  args: ['script-tests', 'node/suspend-idle-source.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-slow-resume-only',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-slow-resume-only.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-direction',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-direction.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-cancels-pending',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-cancels-pending.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-other-clients',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-other-clients.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-last-client',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-last-client.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-partial-release',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-partial-release.lua'],
  env: common_env,
)

test(
  'scripts/node/suspend-fast-start-existing-client',
  script_tester,
  args: ['script-tests', 'node/suspend-fast-start-existing-client.lua'],
  env: common_env,
)
