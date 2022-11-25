valgrind = find_program('valgrind', required: false)
if valgrind.found()

  glib_supp = get_option('glib-supp')
  if glib_supp == ''
    glib_supp = glib_dep.get_variable(pkgconfig: 'prefix')
    glib_supp = glib_supp / 'share' / 'glib-2.0' / 'valgrind' / 'glib.supp'
  endif
  if fs.is_file(glib_supp)
    message('Using glib.supp:', glib_supp)
  else
    message('glib.supp not found, valgrind tests will not work correctly')
  endif

  valgrind_env = environment({
    'G_SLICE': 'always-malloc',
  })

  add_test_setup('valgrind',
    exe_wrapper: [ valgrind,
      '--suppressions=' + glib_supp,
      '--leak-check=full',
      '--gen-suppressions=all',
      '--error-exitcode=3',
      '--keep-debuginfo=yes',
    ],
    env: valgrind_env,
    timeout_multiplier: 2)
endif

# The common test environment
common_test_env = environment({
  'HOME': '/invalid',
  'XDG_RUNTIME_DIR': '/invalid',
  'PIPEWIRE_RUNTIME_DIR': '/tmp',
  'XDG_CONFIG_HOME': meson.current_build_dir() / '.config',
  'XDG_STATE_HOME': meson.current_build_dir() / '.local' / 'state',
  'FILE_MONITOR_DIR': meson.current_build_dir() / '.local' / 'file_monitor',
  'WIREPLUMBER_CONFIG_DIR': '/invalid',
  'WIREPLUMBER_DATA_DIR': meson.current_source_dir() / '..' / 'src',
  'WIREPLUMBER_MODULE_DIR': meson.current_build_dir() / '..' / 'modules',
  'WIREPLUMBER_DEBUG': '7',
})

spa_plugindir = spa_dep.get_variable(
  pkgconfig: 'plugindir', internal: 'plugindir', default_value: '')
pipewire_moduledir = pipewire_dep.get_variable(
  pkgconfig: 'moduledir', internal: 'moduledir', default_value: '')
pipewire_confdatadir = pipewire_dep.get_variable(
  pkgconfig: 'confdatadir', internal: 'confdatadir', default_value: '')

if spa_plugindir != ''
  common_test_env.set('SPA_PLUGIN_DIR', spa_plugindir)
endif
if pipewire_moduledir != ''
  common_test_env.set('PIPEWIRE_MODULE_DIR', pipewire_moduledir)
endif
if pipewire_confdatadir != ''
  common_test_env.set('PIPEWIRE_CONFIG_DIR', pipewire_confdatadir)
endif

subdir('wp')
if build_modules
  subdir('wplua')
  subdir('modules')
endif
subdir('examples')
