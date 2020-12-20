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

reserve_device_interface_src = gnome.gdbus_codegen('reserve-device-interface',
  sources: 'module-dbus-reservation/org.freedesktop.ReserveDevice1.xml',
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
  'wireplumber-module-dbus-reservation',
  [
    'module-dbus-reservation.c',
    'module-dbus-reservation/reserve-device.c',
    'module-dbus-reservation/dbus-device-reservation.c',
    reserve_device_interface_src,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-dbus-reservation"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, giounix_dep],
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

# shared_library(
#   'wireplumber-module-session-settings',
#   [
#     'module-session-settings.c',
#   ],
#   c_args : [common_c_args, '-DG_LOG_DOMAIN="m-session-settings"'],
#   install : true,
#   install_dir : wireplumber_module_dir,
#   dependencies : [wp_dep],
# )

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
  'wireplumber-module-endpoint-creation',
  [
    'module-endpoint-creation/generic-creation.c',
    'module-endpoint-creation/limited-creation.c',
    'module-endpoint-creation/limited-creation-bluez5.c',
    'module-endpoint-creation/parser-endpoint.c',
    'module-endpoint-creation/parser-streams.c',
    'module-endpoint-creation.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-endpoint-creation"'],
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
    'module-lua-scripting/engine.c',
    'module-lua-scripting/api.c',
     m_lua_scripting_resources,
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-lua-scripting"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep, wplua_dep],
)
