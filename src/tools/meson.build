executable('wpctl',
  'wpctl.c',
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wpctl"',
  ],
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
)
