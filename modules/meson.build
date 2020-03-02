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
  'wireplumber-module-jack-device',
  [
    'module-jack-device.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-jack-device"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-pipewire',
  [
    'module-pipewire.c',
    'module-pipewire/algorithms.c',
    'module-pipewire/simple-endpoint-link.c',
    'module-pipewire/video-endpoint.c',
    'module-pipewire/audio-softdsp-endpoint.c',
    'module-pipewire/audio-softdsp-endpoint/stream.c',
    'module-pipewire/audio-softdsp-endpoint/adapter.c',
    'module-pipewire/audio-softdsp-endpoint/convert.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pipewire"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [gio_dep, wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-config-static-nodes',
  [
    'module-config-static-nodes/parser-node.c',
    'module-config-static-nodes/context.c',
    'module-config-static-nodes.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-config-static-nodes"'],
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
	'module-config-policy/config-policy.c',
    'module-config-policy.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-config-policy"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, wptoml_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-session',
  [
    'module-session.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-session"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)
