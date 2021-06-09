common_deps = [wpipc_dep, glib_dep]
common_env = environment({
  'G_SLICE': 'debug-blocks',
  'G_TEST_SRCDIR': meson.current_source_dir(),
  'G_TEST_BUILDDIR': meson.current_build_dir(),
})

test(
  'test-wpipc-sender-receiver',
  executable('test-sender-receiver', 'sender-receiver.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-wpipc-protocol',
  executable('test-protocol', 'protocol.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)

test(
  'test-wpipc-client-server',
  executable('test-client-server', 'client-server.c', dependencies: common_deps),
  env: common_env,
  workdir : meson.current_source_dir(),
)
