wp_lib_sources = files(
  'client.c',
  'component-loader.c',
  'core.c',
  'device.c',
  'endpoint.c',
  'error.c',
  'factory.c',
  'global-proxy.c',
  'iterator.c',
  'link.c',
  'log.c',
  'metadata.c',
  'settings.c',
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
  'session-item.c',
  'si-factory.c',
  'si-interfaces.c',
  'spa-json.c',
  'spa-pod.c',
  'spa-type.c',
  'state.c',
  'transition.c',
  'wp.c',
)

wp_lib_priv_sources = files(
  'private/pipewire-object-mixin.c',
)

wp_lib_headers = files(
  'client.h',
  'component-loader.h',
  'core.h',
  'defs.h',
  'device.h',
  'endpoint.h',
  'error.h',
  'global-proxy.h',
  'iterator.h',
  'link.h',
  'log.h',
  'metadata.h',
  'settings.h',
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
  'session-item.h',
  'si-factory.h',
  'si-interfaces.h',
  'spa-json.h',
  'spa-pod.h',
  'spa-type.h',
  'state.h',
  'transition.h',
  'wp.h',
  'factory.h',
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
wpenums_include_dir = include_directories('.')

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
  wp_lib_sources, wp_lib_priv_sources, wpenums_c, wpenums_h, wpversion,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DWIREPLUMBER_DEFAULT_MODULE_DIR="@0@"'.format(wireplumber_module_dir),
    '-DWIREPLUMBER_DEFAULT_CONFIG_DIR="@0@"'.format(wireplumber_config_dir),
    '-DWIREPLUMBER_DEFAULT_DATA_DIR="@0@"'.format(wireplumber_data_dir),
    '-DBUILDING_WP',
  ],
  install: true,
  include_directories: wp_lib_include_dir,
  dependencies : [gobject_dep, gmodule_dep, gio_dep, pipewire_dep],
  soversion: wireplumber_so_version,
  version: meson.project_version(),
)

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
