wp_sources = [
  'main.c',
]

executable('wireplumber',
  wp_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wireplumber"'
  ],
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep],
)

install_data('wireplumber.conf',
  install_dir: join_paths(get_option('sysconfdir'), 'wireplumber')
)
