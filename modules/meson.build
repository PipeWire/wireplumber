common_c_args = [
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED',
]

shared_library(
  'wireplumber-module-client-permissions',
  [
    'module-client-permissions.c'
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-client-permissions"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

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
  'wireplumber-module-node-suspension',
  [
    'module-node-suspension.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-node-suspension"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-default-metadata',
  [
    'module-default-metadata.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-default-metadata"'],
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

shared_library(
  'wireplumber-module-si-adapter',
  [
    'module-si-adapter.c',
    'module-si-adapter/algorithms.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-adapter"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-convert',
  [
    'module-si-convert.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-convert"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-fake-stream',
  [
    'module-si-fake-stream.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-fake-stream"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-simple-node-endpoint',
  [
    'module-si-simple-node-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-simple-node-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-audio-softdsp-endpoint',
  [
    'module-si-audio-softdsp-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-audio-softdsp-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-monitor-endpoint',
  [
    'module-si-monitor-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-monitor-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-si-bluez5-endpoint',
  [
    'module-si-bluez5-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-bluez5-endpoint"'],
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
