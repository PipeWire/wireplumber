wp_lib_sources = files(
  'base-dirs.c',
  'client.c',
  'component-loader.c',
  'conf.c',
  'core.c',
  'device.c',
  'error.c',
  'event.c',
  'event-dispatcher.c',
  'event-hook.c',
  'factory.c',
  'global-proxy.c',
  'iterator.c',
  'json-utils.c',
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
  'private/internal-comp-loader.c',
  'private/registry.c',
)

wp_lib_headers = files(
  'base-dirs.h',
  'client.h',
  'component-loader.h',
  'conf.h',
  'core.h',
  'defs.h',
  'device.h',
  'error.h',
  'event.h',
  'event-dispatcher.h',
  'event-hook.h',
  'global-proxy.h',
  'iterator.h',
  'json-utils.h',
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

wpbuildbasedirs_data = configuration_data()
wpbuildbasedirs_data.set('BUILD_SYSCONFDIR', '"@0@"'.format(get_option('prefix') / get_option('sysconfdir')))
wpbuildbasedirs_data.set('BUILD_DATADIR', '"@0@"'.format(get_option('prefix') / get_option('datadir')))
wpbuildbasedirs_data.set('BUILD_LIBDIR', '"@0@"'.format(get_option('prefix') / get_option('libdir')))
wpbuildbasedirs_data.set('BUILD_LOCALEDIR', '"@0@"'.format(get_option('prefix') / get_option('localedir')))
wpbuildbasedirs = configure_file (
  output : 'wpbuildbasedirs.h',
  configuration : wpbuildbasedirs_data,
)

wp_lib = library('wireplumber-' + wireplumber_api_version,
  wp_lib_sources, wp_lib_priv_sources, wpenums_c, wpenums_h, wpversion, wpbuildbasedirs,
  c_args : [
    '-DBUILDING_WP',
  ],
  install: true,
  include_directories: wp_lib_include_dir,
  dependencies : [gobject_dep, gmodule_dep, gio_dep, pipewire_dep, libintl_dep],
  soversion: wireplumber_so_version,
  version: wireplumber_libversion,
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
