wplua_lib_sources = [
  'boxed.c',
  'closure.c',
  'object.c',
  'userdata.c',
  'value.c',
  'wplua.c',
]

wplua_resources = gnome.compile_resources(
    'wplua-resources',
    'gresource.xml',
    c_name: '_wplua',
    extra_args: '--manual-register',
    source_dir: meson.current_source_dir())

wplua_lib = static_library('wplua-' + wireplumber_api_version,
  [ wplua_lib_sources, wplua_resources ],
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
