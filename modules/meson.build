shared_library(
  'wireplumber-module-settings',
  [
    'module-settings.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

dbus_connection_enums = gnome.mkenums_simple('dbus-connection-enums',
  sources: [ 'dbus-connection-state.h' ],
)
shared_library(
  'wireplumber-module-dbus-connection',
  [
    'module-dbus-connection.c',
    dbus_connection_enums,
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep],
)

shared_library(
  'wireplumber-module-default-nodes-api',
  [
    'module-default-nodes-api.c',
  ],
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
  include_directories: reserve_device_includes,
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, giounix_dep],
)

shared_library(
  'wireplumber-module-portal-permissionstore',
  [
    'module-portal-permissionstore.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, giounix_dep],
)

shared_library(
  'wireplumber-module-si-audio-adapter',
  [
    'module-si-audio-adapter.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-node',
  [
    'module-si-node.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-standard-link',
  [
    'module-si-standard-link.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

subdir('module-lua-scripting')
shared_library(
  'wireplumber-module-lua-scripting',
  [
    'module-lua-scripting/module.c',
    'module-lua-scripting/script.c',
    'module-lua-scripting/api/pod.c',
    'module-lua-scripting/api/json.c',
    'module-lua-scripting/api/api.c',
     m_lua_scripting_resources,
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, wplua_dep, libintl_dep],
)

shared_library(
  'wireplumber-module-mixer-api',
  [
    'module-mixer-api.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, mathlib],
)

shared_library(
  'wireplumber-module-file-monitor-api',
  [
    'module-file-monitor-api.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-log-settings',
  [
    'module-log-settings.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

if libsystemd_dep.found() or libelogind_dep.found()
  shared_library(
    'wireplumber-module-logind',
    [
      'module-logind.c',
    ],
    install : true,
    install_dir : wireplumber_module_dir,
    dependencies : [wp_dep, pipewire_dep, libsystemd_dep, libelogind_dep],
  )
endif

shared_library(
  'wireplumber-module-standard-event-source',
  [
    'module-standard-event-source.c',
  ],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)
