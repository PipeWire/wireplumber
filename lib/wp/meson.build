wp_lib_sources = files(
  'base-endpoint.c',
  'client.c',
  'configuration.c',
  'core.c',
  'device.c',
  'endpoint.c',
  'error.c',
  'factory.c',
  'iterator.c',
  'link.c',
  'module.c',
  'node.c',
  'object-manager.c',
  'policy.c',
  'port.c',
  'properties.c',
  'proxy.c',
  'session.c',
  'spa-type.c',
  'spa-props.c',
)

wp_lib_headers = files(
  'base-endpoint.h',
  'client.h',
  'configuration.h',
  'core.h',
  'defs.h',
  'device.h',
  'endpoint.h',
  'error.h',
  'factory.h',
  'iterator.h',
  'link.h',
  'module.h',
  'node.h',
  'object-manager.h',
  'policy.h',
  'port.h',
  'properties.h',
  'proxy.h',
  'session.h',
  'spa-type.h',
  'wp.h',
)

install_headers(wp_lib_headers,
  subdir : join_paths('wireplumber-' + wireplumber_api_version, 'wp')
)

enums = gnome.mkenums_simple('wpenums',
  sources: wp_lib_headers,
  header_prefix: '#include "wp/defs.h"',
  decorator: 'WP_API',
  install_header: true,
  install_dir: join_paths(get_option('includedir'), 'wireplumber-' + wireplumber_api_version, 'wp'),
)
wpenums_h = enums[1]
wpenums_c = enums[0]
wp_gen_sources = [wpenums_h]

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, wpenums_c, wpenums_h,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="libwireplumber"',
    '-DWIREPLUMBER_DEFAULT_MODULE_DIR="@0@"'.format(wireplumber_module_dir),
    '-DBUILDING_WP',
  ],
  install: true,
  include_directories: wp_lib_include_dir,
  dependencies : [gobject_dep, gmodule_dep, gio_dep, pipewire_dep],
  soversion: wireplumber_so_version,
  version: meson.project_version(),
)

if build_gir
  wp_gir = gnome.generate_gir(wp_lib,
    namespace: 'Wp',
    nsversion: wireplumber_api_version,
    sources: [wp_lib_sources, wp_lib_headers, wpenums_c, wpenums_h],
    includes: ['GLib-2.0', 'GObject-2.0', 'Gio-2.0'],
    install: true,
  )
  wp_gen_sources += wp_gir
endif

wp_dep = declare_dependency(
  link_with: wp_lib,
  sources: wp_gen_sources,
  include_directories: wp_lib_include_dir,
  dependencies: [gobject_dep, gio_dep]
)

pkgconfig.generate(wp_lib,
  libraries: [gobject_dep, gio_dep],
  subdirs: 'wireplumber-' + wireplumber_api_version
)
