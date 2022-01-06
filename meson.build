project('wireplumber', ['c'],
  version : '0.4.6',
  license : 'MIT',
  meson_version : '>= 0.56.0',
  default_options : [
    'warning_level=1',
    'buildtype=debugoptimized',
  ]
)

wireplumber_api_version = '0.4'
wireplumber_so_version = '0'

wireplumber_headers_dir = get_option('includedir') / 'wireplumber-' + wireplumber_api_version / 'wp'

wireplumber_bin_dir = get_option('prefix') / get_option('bindir')
wireplumber_module_dir = get_option('prefix') / get_option('libdir') / 'wireplumber-' + wireplumber_api_version
wireplumber_data_dir = get_option('prefix') / get_option('datadir') / 'wireplumber'
wireplumber_config_dir = get_option('prefix') / get_option('sysconfdir') / 'wireplumber'

cc = meson.get_compiler('c')

glib_req_version = '>= 2.62'
add_project_arguments([
    '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_62',
    '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_62',
  ], language: 'c'
)

glib_dep = dependency('glib-2.0', version : glib_req_version)
gobject_dep = dependency('gobject-2.0', version : glib_req_version)
gmodule_dep = dependency('gmodule-2.0', version : glib_req_version)
gio_dep = dependency('gio-2.0', version : glib_req_version)
giounix_dep = dependency('gio-unix-2.0', version : glib_req_version)
spa_dep = dependency('libspa-0.2', version: '>= 0.2')
pipewire_dep = dependency('libpipewire-0.3', version: '>= 0.3.43')
mathlib = cc.find_library('m')
threads_dep = dependency('threads')

system_lua = get_option('system-lua')
if system_lua
  if get_option('system-lua-version') != 'auto'
    lua_version_requested = get_option('system-lua-version')
    lua_dep = dependency('lua-' + lua_version_requested, required: false)
    if not lua_dep.found()
      lua_dep = dependency('lua' + lua_version_requested, required: false)
    endif

    if not lua_dep.found()
      error('Specified Lua version "' + lua_version_requested + '" not found')
    endif
  else
    lua_dep = dependency('lua-5.4', required: false)
    if not lua_dep.found()
      lua_dep = dependency('lua5.4', required: false)
    endif
    if not lua_dep.found()
      lua_dep = dependency('lua54', required: false)
    endif
    if not lua_dep.found()
      lua_dep = dependency('lua-5.3', required: false)
    endif
    if not lua_dep.found()
      lua_dep = dependency('lua5.3', required: false)
    endif
    if not lua_dep.found()
      lua_dep = dependency('lua53', required: false)
    endif
    if not lua_dep.found()
      lua_dep = dependency('lua', version: ['>=5.3.0'], required: false)
    endif
    if not lua_dep.found()
      error ('Could not find lua. Lua version 5.4 or 5.3 required')
    endif
  endif
else
  lua_proj = subproject('lua', default_options: ['default_library=static'])
  lua_dep = lua_proj.get_variable('lua_dep')
endif
summary({'Lua version': lua_dep.version() + (system_lua ? ' (system)' : ' (built-in)')})

systemd = dependency('systemd', required: get_option('systemd'))
libsystemd_dep = dependency('libsystemd',required: get_option('systemd'))
libelogind_dep = dependency('libelogind', required: get_option('elogind'))
summary({'systemd conf data': systemd.found(),
         'libsystemd': libsystemd_dep.found(),
         'libelogind': libelogind_dep.found()}, bool_yn: true)

gnome = import('gnome')
pkgconfig = import('pkgconfig')
fs = import('fs')

wp_lib_include_dir = include_directories('lib')

common_flags = [
  '-fvisibility=hidden',
  '-Wsuggest-attribute=format',
  '-Wsign-compare',
  '-Wpointer-arith',
  '-Wpointer-sign',
  '-Wformat',
  '-Wformat-security',
  '-Wimplicit-fallthrough',
  '-Wmissing-braces',
  '-Wtype-limits',
  '-Wvariadic-macros',
  '-Wno-missing-field-initializers',
  '-Wno-unused-parameter',
  '-Wno-pedantic',
  '-Wold-style-declaration',
  '-Wunused-result',
]
add_project_arguments(cc.get_supported_arguments(common_flags), language: 'c')

subdir('lib')
subdir('docs')
subdir('modules')
subdir('src')

if get_option('tests')
  subdir('tests')
endif

conf_uninstalled = configuration_data()
conf_uninstalled.set('MESON', '')
conf_uninstalled.set('MESON_SOURCE_ROOT', meson.project_source_root())
conf_uninstalled.set('MESON_BUILD_ROOT', meson.project_build_root())

wp_uninstalled = configure_file(
  input : 'wp-uninstalled.sh',
  output : 'wp-uninstalled.sh.in',
  configuration : conf_uninstalled,
)

wireplumber_uninstalled = custom_target('wp-uninstalled',
  output : 'wp-uninstalled.sh',
  input : wp_uninstalled,
  build_by_default : true,
  command : ['cp', '@INPUT@', '@OUTPUT@'],
)

if meson.version().version_compare('>= 0.58')
  builddir = meson.project_build_root()
  srcdir = meson.project_source_root()

  devenv = environment({
    'WIREPLUMBER_MODULE_DIR': builddir / 'modules',
    'WIREPLUMBER_CONFIG_DIR': srcdir / 'src' / 'config',
    'WIREPLUMBER_DATA_DIR': srcdir / 'src',
  })

  meson.add_devenv(devenv)
endif
