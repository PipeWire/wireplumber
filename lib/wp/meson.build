wp_lib_sources = [
  'error.c',
  'interface-impl.c',
  'object.c',
]

wp_lib_headers = [
  'error.h',
  'interface-impl.h',
  'object.h',
]

enums = gnome.mkenums_simple('wpenums', sources: wp_lib_headers)

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, enums,
  c_args : [ '-D_GNU_SOURCE', '-DG_LOG_USE_STRUCTURED' ],
  install: true,
  dependencies : [gobject_dep, pipewire_dep],
)

gnome.generate_gir(wp_lib,
  namespace: 'Wp',
  nsversion: wireplumber_api_version,
  sources: [wp_lib_sources, wp_lib_headers],
  includes: ['GLib-2.0', 'GObject-2.0'],
  install: true,
)
