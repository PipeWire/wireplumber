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
  'wireplumber-module-mixer',
  [
    'module-mixer.c'
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-mixer"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep],
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
  'wireplumber-module-pipewire',
  [
    'module-pipewire.c',
    'module-pipewire/simple-endpoint-link.c',
    'module-pipewire/simple-endpoint.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pipewire"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [gio_dep, wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-pw-audio-client',
  [
    'module-pw-audio-client.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pw-audio-client"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)


shared_library(
  'wireplumber-module-pw-alsa-udev',
  [
    'module-pw-alsa-udev.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pw-alsa-udev"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-pw-bluez',
  [
    'module-pw-bluez.c',
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pw-bluez"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-pw-audio-softdsp-endpoint',
  [
    'module-pw-audio-softdsp-endpoint.c',
    'module-pw-audio-softdsp-endpoint/stream.c',
    'module-pw-audio-softdsp-endpoint/adapter.c',
    'module-pw-audio-softdsp-endpoint/convert.c',
 ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-pw-audio-softdsp-endpoint"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [gio_dep, wp_dep, pipewire_dep],
)

shared_library(
  'wireplumber-module-simple-policy',
  [
    'module-simple-policy.c'
  ],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="m-simple-policy"'],
  install : true,
  install_dir : wireplumber_module_dir,
  dependencies : [wp_dep, pipewire_dep],
)
