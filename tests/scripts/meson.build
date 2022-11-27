common_deps = [wplua_dep, pipewire_dep, wp_dep]
common_env = common_test_env
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
  'test-lua-event-policy-hooks',
  script_tester,
  args: ['policy-tests', 'policy-hooks.lua'],
  env: common_env,
)
