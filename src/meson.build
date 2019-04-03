wp_sources = [
  'core.c',
  'loop-source.c',
  'main.c',
  'utils.c',
]

executable('wireplumber',
  wp_sources,
  c_args : [ '-D_GNU_SOURCE', '-DG_LOG_USE_STRUCTURED' ],
  install: true,
  dependencies : [glib_dep, pipewire_dep],
)
