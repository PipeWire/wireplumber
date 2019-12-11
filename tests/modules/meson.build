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

test(
  'test-config-endpoint',
  executable('test-config-endpoint',
    [
      'config-endpoint.c',
      'config-endpoint/endpoint-audiotestsrc.c',
      '../../modules/module-config-endpoint/parser-endpoint.c',
      '../../modules/module-config-endpoint/parser-streams.c',
      '../../modules/module-config-endpoint/context.c',
    ],
    dependencies: common_deps + [wptoml_dep]),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-config-policy',
  executable('test-config-policy',
    [
      'config-policy.c',
      'config-policy/context.c',
      'config-policy/endpoint-fake.c',
      'config-policy/endpoint-link-fake.c',
      '../../modules/module-config-policy/config-policy.c',
      '../../modules/module-config-policy/parser-streams.c',
      '../../modules/module-config-policy/parser-endpoint-link.c'
    ],
    dependencies: common_deps + [wptoml_dep]),
  env: common_env,
  workdir : meson.current_source_dir(),
)
