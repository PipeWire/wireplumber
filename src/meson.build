wp_sources = [
  'main.c',
]

wireplumber_config_file = join_paths(wireplumber_config_dir, 'wireplumber.conf')
install_data('wireplumber.conf',
  install_dir: wireplumber_config_dir
)

executable('wireplumber',
  wp_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wireplumber"',
    '-DWIREPLUMBER_DEFAULT_CONFIG_FILE="@0@"'.format(wireplumber_config_file),
  ],
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
)
