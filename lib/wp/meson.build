wp_lib_sources = [
  'base-endpoint.c',
  'configuration.c',
  'core.c',
  'endpoint.c',
  'error.c',
  'exported.c',
  'factory.c',
  'module.c',
  'monitor.c',
  'object-manager.c',
  'policy.c',
  'properties.c',
  'proxy.c',
  'proxy-client.c',
  'proxy-link.c',
  'proxy-node.c',
  'proxy-port.c',
  'session.c',
  'spa-props.c',
]

wp_lib_headers = [
  'base-endpoint.h',
  'configuration.h',
  'endpoint.h',
  'core.h',
  'error.h',
  'exported.h',
  'factory.h',
  'module.h',
  'monitor.h',
  'object-manager.h',
  'policy.h',
  'properties.h',
  'proxy.h',
  'proxy-client.h',
  'proxy-node.h',
  'proxy-port.h',
  'proxy-link.h',
  'session.h',
  'wp.h',
]

install_headers(wp_lib_headers,
  subdir : join_paths('wireplumber-' + wireplumber_api_version, 'wp')
)

enums = gnome.mkenums_simple('wpenums',
  sources: wp_lib_headers,
  install_header: true,
  install_dir: join_paths(get_option('includedir'), 'wireplumber-' + wireplumber_api_version, 'wp'),
)

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
  dependencies : [gobject_dep, gmodule_dep, gio_dep, pipewire_dep],
  soversion: wireplumber_so_version,
  version: meson.project_version(),
)

if get_option('introspection')
  gnome.generate_gir(wp_lib,
    namespace: 'Wp',
    nsversion: wireplumber_api_version,
    sources: [wp_lib_sources, wp_lib_headers, enums],
    includes: ['GLib-2.0', 'GObject-2.0', 'Gio-2.0'],
    install: true,
  )
endif

wp_dep = declare_dependency(
  link_with: wp_lib,
  sources: enums[1],
  include_directories: wp_lib_include_dir,
  dependencies: [gobject_dep]
)

pkgconfig.generate(wp_lib,
  libraries: [gobject_dep, gio_dep],
  subdirs: 'wireplumber-' + wireplumber_api_version
)
