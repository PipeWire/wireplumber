if get_option('systemd-system-service') or get_option('systemd-user-service')
  systemd_config = configuration_data()
  systemd_config.set('WP_BINARY', wireplumber_bin_dir / 'wireplumber')
  systemd_system_unit_dir = ''
  systemd_user_unit_dir = ''

  if systemd.found()
    systemd_system_unit_dir = systemd.get_variable(
      pkgconfig: 'systemdsystemunitdir',
      pkgconfig_define: ['prefix', get_option('prefix')])
    systemd_user_unit_dir = systemd.get_variable(
      pkgconfig: 'systemduserunitdir',
      pkgconfig_define: ['prefix', get_option('prefix')])
  endif

  # system service
  if get_option('systemd-system-service')
    if get_option('systemd-system-unit-dir') != ''
      systemd_system_unit_dir = get_option('systemd-system-unit-dir')
    endif

    if systemd_system_unit_dir != ''
      subdir('system')
    endif
  endif

  # user service
  if get_option('systemd-user-service')

    if get_option('systemd-user-unit-dir') != ''
      systemd_user_unit_dir = get_option('systemd-user-unit-dir')
    endif

    if systemd_user_unit_dir != ''
      subdir('user')
    endif
  endif
endif
