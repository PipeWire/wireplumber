systemd = dependency('systemd', required: get_option('systemd'))

if systemd.found()
  systemd_config = configuration_data()
  systemd_config.set('WP_BINARY', join_paths(wireplumber_bin_dir, 'wireplumber'))

  # system service
  if get_option('systemd-system-service')
    systemd_system_unit_dir = systemd.get_pkgconfig_variable(
        'systemd_system_unit_dir',
        define_variable : ['prefix', get_option('prefix')])

    if get_option('systemd-system-unit-dir') != ''
      systemd_system_unit_dir = get_option('systemd-system-unit-dir')
    endif

    subdir('system')
  endif

  # user service
  if get_option('systemd-user-service')
    systemd_user_unit_dir = systemd.get_pkgconfig_variable(
        'systemd_user_unit_dir',
        define_variable : ['prefix', get_option('prefix')])

    if get_option('systemd-user-unit-dir') != ''
      systemd_user_unit_dir = get_option('systemd-user-unit-dir')
    endif

    subdir('user')
  endif
endif
