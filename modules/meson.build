common_c_args = [
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED',
]

shared_library(
  'wireplumber-module-metadata',
  [
    'module-metadata.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-metadata"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-default-profile',
  [
    'module-default-profile.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-default-profile"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-default-routes',
  [
    'module-default-routes.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-default-routes"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-device-activation',
  [
    'module-device-activation.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-device-activation"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-default-nodes',
  [
    'module-default-nodes.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-default-nodes"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-default-nodes-api',
  [
    'module-default-nodes-api.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-default-nodes-api"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

subdir('module-reserve-device')
shared_library(
  'wireplumber-module-reserve-device',
  [
    'module-reserve-device/plugin.c',
    'module-reserve-device/reserve-device.c',
    'module-reserve-device/transitions.c',
    reserve_device_interface_src,
    reserve_device_enums,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-reserve-device"'],
  include_directories: reserve_device_includes,
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, giounix_dep],
)

subdir('module-portal-permissionstore')
shared_library(
  'wireplumber-module-portal-permissionstore',
  [
    'module-portal-permissionstore/plugin.c',
    portal_permissionstore_enums,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-portal-permissionstore"'],
  include_directories: portal_permissionstore_includes,
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, giounix_dep],
)

shared_library(
  'wireplumber-module-si-audio-adapter',
  [
    'module-si-audio-adapter.c',
    'module-si-audio-adapter/audio-utils.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-audio-adapter"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-audio-endpoint',
  [
    'module-si-audio-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-audio-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-node',
  [
    'module-si-node.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-node"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-standard-link',
  [
    'module-si-standard-link.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-standard-link"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

m_lua_scripting_resources = gnome.compile_resources(
    'm-lua-scripting-resources',
    'module-lua-scripting/gresource.xml',
    source_dir: 'module-lua-scripting',
    c_name: '_m_lua_scripting_resources')

shared_library(
  'wireplumber-module-lua-scripting',
  [
    'module-lua-scripting.c',
    'module-lua-scripting/pod.c',
    'module-lua-scripting/api.c',
    'module-lua-scripting/config.c',
     m_lua_scripting_resources,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-lua-scripting"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, wplua_dep],
)
