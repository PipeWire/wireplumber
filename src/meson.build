if build_tools
  subdir('tools')
endif

if build_daemon
  subdir('config')
  subdir('systemd')

  install_subdir('scripts',
    install_dir: wireplumber_data_dir,
    strip_directory : false
  )

  wp_sources = [
    'main.c',
  ]

  wireplumber = executable('wireplumber',
    wp_sources,
    c_args : [
      '-D_GNU_SOURCE',
      '-DG_LOG_USE_STRUCTURED',
    ],
    install: true,
    dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
  )
endif
