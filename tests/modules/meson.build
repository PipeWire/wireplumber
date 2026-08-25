common_deps = [gobject_dep, gio_dep, wp_dep, pipewire_dep]
common_env = common_test_env
common_env.set('G_TEST_SRCDIR', meson.current_source_dir())
common_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

if get_option('dbus-tests')
  test(
    'modules/dbus-connection',
    executable('test-dbus-connection', 'dbus-connection.c',
        dependencies: common_deps),
    env: common_env,
  )

  test(
    'modules/reserve-device',
    executable('test-reserve-device', 'reserve-device.c',
      dependencies: common_deps),
    env: common_env,
  )
endif

test(
  'modules/file-monitor',
  executable('test-file-monitor', 'file-monitor.c',
    dependencies: common_deps),
  env: common_env,
)
