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



install_headers(wpipc_lib_headers,
  install_dir : join_paths(get_option('includedir'), 'wpipc-' + wireplumber_api_version, 'wpipc')
)

wpipc_lib = library('wpipc-' + wireplumber_api_version,
  wpipc_lib_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wpipc"',
  ],
  install: true,
  dependencies : [dependency('threads'), spa_dep],
)

wpipc_dep = declare_dependency(
  link_with: wpipc_lib,
  include_directories: wp_lib_include_dir,
  dependencies: [spa_dep],
)
