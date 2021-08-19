# disabled temporarily
if false
executable('audiotestsrc-play',
  'audiotestsrc-play.c',
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="audiotestsrc-play"',
  ],
  install: false,
  dependencies : [giounix_dep, wp_dep, pipewire_dep],
)
endif
