common_c_args = [
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED',
]

shared_library(
  'wireplumber-module-anatole-policy',
  [
    'module-anatole-policy.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-anatole-policy"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, anatole_dep],
)

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

reserve_device_interface_src = gnome.gdbus_codegen('reserve-device-interface',
  sources: 'module-device-activation/org.freedesktop.ReserveDevice1.xml',
  interface_prefix : 'org.freedesktop.ReserveDevice1.',
  namespace : 'Wp'
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
  'wireplumber-module-monitor',
  [
    'module-monitor.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-monitor"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-device-activation',
  [
    'module-device-activation.c',
    'module-device-activation/reserve-node.c',
    'module-device-activation/reserve-device.c',
    'module-device-activation/dbus-device-reservation.c',
    reserve_device_interface_src,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-device-activation"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, giounix_dep],
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
    'module-lua-scripting/anatole-config-parser.c',
    'module-lua-scripting/graph-loader.c',
    m_lua_scripting_resources,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-lua-scripting"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, anatole_dep],
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
  'wireplumber-module-session-settings',
  [
    'module-session-settings.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-session-settings"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep],
)

shared_library(
  'wireplumber-module-config-static-objects',
  [
    'module-config-static-objects/parser-device.c',
    'module-config-static-objects/parser-node.c',
    'module-config-static-objects/context.c',
    'module-config-static-objects.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-config-static-objects"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, wptoml_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-config-endpoint',
  [
    'module-config-endpoint/parser-endpoint.c',
    'module-config-endpoint/parser-streams.c',
    'module-config-endpoint/context.c',
    'module-config-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-config-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, wptoml_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-config-policy',
  [
    'module-config-policy/parser-endpoint-link.c',
    'module-config-policy/context.c',
    'module-config-policy.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-config-policy"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, wptoml_dep, pipewire_dep],
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
  'wireplumber-module-si-standard-link',
  [
    'module-si-standard-link.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-si-standard-link"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)
