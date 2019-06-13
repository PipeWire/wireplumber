wp_lib_sources = [
  'core.c',
  'endpoint.c',
  'error.c',
  'factory.c',
  'module.c',
  'policy.c',
]

wp_lib_headers = [
  'core.h',
  'endpoint.h',
  'error.h',
  'factory.h',
  'module.h',
  'policy.h',
  'wp.h',
]

install_headers(wp_lib_headers,
  subdir : join_paths('wireplumber-' + wireplumber_api_version, 'wp')
)

enums = gnome.mkenums_simple('wpenums', sources: wp_lib_headers)

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, enums,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="libwireplumber"',
    '-DWIREPLUMBER_DEFAULT_MODULE_DIR="@0@"'.format(wireplumber_module_dir),
  ],
  install: true,
  include_directories: wp_lib_include_dir,
  dependencies : [gobject_dep, gmodule_dep],
  soversion: wireplumber_so_version,
  version: meson.project_version(),
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
