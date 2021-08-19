wp_sources = [
  'main.c',
]

install_subdir('config',
  install_dir: wireplumber_data_dir,
  strip_directory : true
)
install_subdir('scripts',
  install_dir: wireplumber_data_dir,
  strip_directory : false
)

subdir('systemd')
subdir('tools')

executable('wireplumber',
  wp_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wireplumber"',
  ],
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
)
