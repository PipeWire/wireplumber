wp_sources = [
  'core.c',
  'loop-source.c',
  'main.c',
  'module-loader.c',
  'plugin-registry-impl.c',
  'proxy-registry-impl.c',
  'utils.c',
]

executable('wireplumber',
  wp_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wireplumber"'
  ],
  install: true,
  dependencies : [gobject_dep, gmodule_dep, gio_dep, pipewire_dep, wp_dep],
)
