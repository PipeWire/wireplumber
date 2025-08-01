project('wireplumber', ['c'],
  version : '0.5.10',
  license : 'MIT',
  meson_version : '>= 0.59.0',
  default_options : [
    'warning_level=1',
    'buildtype=debugoptimized',
  ]
)

wireplumber_version = meson.project_version()
version_arr = wireplumber_version.split('.')
wireplumber_version_major = version_arr[0]
wireplumber_version_minor = version_arr[1]
wireplumber_version_micro = version_arr[2]

wireplumber_api_version = '0.5'
wireplumber_so_version = '0'
wireplumber_libversion_minor = wireplumber_version_major.to_int() * 1000 + wireplumber_version_minor.to_int() * 100 + wireplumber_version_micro.to_int()
wireplumber_libversion = '@0@.@1@.0'.format(wireplumber_so_version, wireplumber_libversion_minor)

wireplumber_headers_dir = get_option('includedir') / 'wireplumber-' + wireplumber_api_version / 'wp'

wireplumber_bin_dir = get_option('prefix') / get_option('bindir')
wireplumber_module_dir = get_option('prefix') / get_option('libdir') / 'wireplumber-' + wireplumber_api_version
wireplumber_data_dir = get_option('prefix') / get_option('datadir') / 'wireplumber'
wireplumber_config_dir = get_option('prefix') / get_option('sysconfdir') / 'wireplumber'
wireplumber_locale_dir = get_option('prefix') / get_option('localedir')
wireplumber_doc_dir = get_option('prefix') / get_option('datadir') / 'doc' / 'wireplumber'

cc = meson.get_compiler('c')

glib_req_version = '>= 2.68'
add_project_arguments([
    '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_68',
    '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_68',
  ], language: 'c'
)

add_project_arguments([
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
  ], language: 'c'
)

check_short_name = '''#define _GNU_SOURCE
#include <errno.h>
void main() { program_invocation_short_name; }
'''
if cc.compiles(check_short_name, name : 'check short_name')
  add_project_arguments([
      '-DHAS_SHORT_NAME',
    ], language: 'c'
  )
endif

build_modules = get_option('modules')
build_daemon = get_option('daemon')
if build_daemon and not build_modules
  error('\'modules\' option is required to be true when the \'daemon\' option is true')
endif
build_tools = get_option('tools')
if build_tools and not build_modules
  error('\'modules\' option is required to be true when the \'tools\' option is enabled')
endif

glib_dep = dependency('glib-2.0', version : glib_req_version)
gobject_dep = dependency('gobject-2.0', version : glib_req_version)
gmodule_dep = dependency('gmodule-2.0', version : glib_req_version)
gio_dep = dependency('gio-2.0', version : glib_req_version)
giounix_dep = dependency('gio-unix-2.0', version : glib_req_version)
spa_dep = dependency('libspa-0.2', version: '>= 0.2')
pipewire_dep = dependency('libpipewire-0.3', version: '>= 1.0.2')
mathlib = cc.find_library('m')
threads_dep = dependency('threads')
libintl_dep = dependency('intl')

if build_modules
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
endif

if build_modules
  systemd = dependency('systemd', required: get_option('systemd'))
  libsystemd_dep = dependency('libsystemd',required: get_option('systemd'))
  libelogind_dep = dependency('libelogind', required: get_option('elogind'))
  summary({'systemd conf data': systemd.found(),
          'libsystemd': libsystemd_dep.found(),
          'libelogind': libelogind_dep.found()}, bool_yn: true)
endif

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

common_args = [
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED',
  '-DWP_USE_LOCAL_LOG_TOPIC_IN_G_LOG',
]
add_project_arguments(common_args, language: 'c')

i18n_conf = files()

subdir('lib')
if build_modules
  subdir('modules')
endif
subdir('src')
subdir('po')
subdir('docs')

if get_option('tests')
  subdir('tests')
endif

builddir = meson.project_build_root()
srcdir = meson.project_source_root()

conf_uninstalled = configuration_data()
conf_uninstalled.set('MESON', '')
conf_uninstalled.set('MESON_SOURCE_ROOT', srcdir)
conf_uninstalled.set('MESON_BUILD_ROOT', builddir)

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

devenv = environment({
  'WIREPLUMBER_MODULE_DIR': builddir / 'modules',
  'WIREPLUMBER_CONFIG_DIR': srcdir / 'src' / 'config',
  'WIREPLUMBER_DATA_DIR': srcdir / 'src',
})

meson.add_devenv(devenv)
