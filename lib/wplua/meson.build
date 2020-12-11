wplua_lib_sources = [
  'boxed.c',
  'closure.c',
  'object.c',
  'userdata.c',
  'value.c',
  'wplua.c',
]

wplua_lib = static_library('wplua-' + wireplumber_api_version,
  wplua_lib_sources,
  c_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="wplua"',
  ],
  install: false,
  include_directories: wp_lib_include_dir,
  dependencies : [wp_dep, lua_dep],
)

wplua_dep = declare_dependency(
  link_with: wplua_lib,
  include_directories: wp_lib_include_dir,
  dependencies: [wp_dep, lua_dep],
)
