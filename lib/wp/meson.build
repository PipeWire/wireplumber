wp_lib_sources = [
  'core-interfaces.c',
  'error.c',
  'interface-impl.c',
  'object.c',
  'plugin.c',
  'proxy.c',
]

wp_lib_headers = [
  'core-interfaces.h',
  'error.h',
  'interface-impl.h',
  'object.h',
  'plugin.h',
  'proxy.h',
]

enums = gnome.mkenums_simple('wpenums', sources: wp_lib_headers)

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, enums,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="libwireplumber"'
  ],
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

wp_dep = declare_dependency(
  link_with: wp_lib,
  include_directories: wp_lib_include_dir,
  dependencies: [gobject_dep]
)
