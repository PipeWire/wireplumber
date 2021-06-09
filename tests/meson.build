valgrind = find_program('valgrind', required: false)
if valgrind.found()

  glib_supp = get_option('glib-supp')
  if glib_supp == ''
    glib_supp = glib_dep.get_pkgconfig_variable('prefix')
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

subdir('wp')
subdir('wplua')
subdir('wpipc', if_found: wpipc_dep)
subdir('modules')
subdir('examples')
