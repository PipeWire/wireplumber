if get_option('wpipc').disabled()
  wpipc_dep = disabler()
  subdir_done()
endif

wpipc_lib_sources = files(
  'utils.c',
  'protocol.c',
  'receiver.c',
  'sender.c',
  'client.c',
  'server.c',
)

wpipc_lib_headers = files(
  'protocol.h',
  'receiver.h',
  'sender.h',
  'client.h',
  'server.h',
  'wpipc.h',
)

wpipc_api_version = '0.1'
wpipc_so_version = '0'

wpipc_headers_dir = get_option('includedir') / 'wpipc-' + wpipc_api_version / 'wpipc'

install_headers(wpipc_lib_headers,
  install_dir : wpipc_headers_dir
)

wpipc_lib = library('wpipc-' + wpipc_api_version,
  wpipc_lib_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wpipc"',
  ],
  install: true,
  dependencies : [dependency('threads'), spa_dep],
  soversion: wpipc_so_version,
  version: meson.project_version(),
)

wpipc_dep = declare_dependency(
  link_with: wpipc_lib,
  include_directories: wp_lib_include_dir,
  dependencies: [spa_dep],
)
