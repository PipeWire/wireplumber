wp_lib_sources = files(
  'private/pipewire-object-mixin.c',
  'client.c',
  'configuration.c',
  'core.c',
  'debug.c',
  'device.c',
  'endpoint.c',
  'endpoint-link.c',
  'endpoint-stream.c',
  'error.c',
  'global-proxy.c',
  'iterator.c',
  'link.c',
  'metadata.c',
  'module.c',
  'node.c',
  'object.c',
  'object-interest.c',
  'object-manager.c',
  'plugin.c',
  'port.c',
  'properties.c',
  'proxy.c',
  'proxy-interfaces.c',
  'session.c',
  'session-bin.c',
  'session-item.c',
  'si-factory.c',
  'si-interfaces.c',
  'spa-pod.c',
  'spa-type.c',
  'transition.c',
  'wp.c',
)

wp_lib_headers = files(
  'client.h',
  'configuration.h',
  'core.h',
  'debug.h',
  'defs.h',
  'device.h',
  'endpoint.h',
  'endpoint-link.h',
  'endpoint-stream.h',
  'error.h',
  'global-proxy.h',
  'iterator.h',
  'link.h',
  'metadata.h',
  'module.h',
  'node.h',
  'object.h',
  'object-interest.h',
  'object-manager.h',
  'plugin.h',
  'port.h',
  'properties.h',
  'proxy.h',
  'proxy-interfaces.h',
  'session.h',
  'session-bin.h',
  'session-item.h',
  'si-factory.h',
  'si-interfaces.h',
  'spa-pod.h',
  'spa-type.h',
  'transition.h',
  'wp.h',
)

install_headers(wp_lib_headers,
  install_dir : wireplumber_headers_dir
)

enums = gnome.mkenums_simple('wpenums',
  sources: wp_lib_headers,
  header_prefix: '#include "wp/defs.h"',
  decorator: 'WP_API',
  install_header: true,
  install_dir: wireplumber_headers_dir,
)
wpenums_h = enums[1]
wpenums_c = enums[0]
wp_gen_sources = [wpenums_h]

wpversion_data = configuration_data()
wpversion_data.set('version', meson.project_version())
wpversion_data.set('api_version', wireplumber_api_version)
wpversion = configure_file(
  input : 'wpversion.h.in',
  output : 'wpversion.h',
  configuration : wpversion_data,
  install_dir: wireplumber_headers_dir
)
wp_gen_sources += [wpversion]

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, wpenums_c, wpenums_h, wpversion,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
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
