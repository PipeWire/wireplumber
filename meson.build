project('wireplumber', ['c'],
  version : '0.3.70',
  license : 'MIT',
  meson_version : '>= 0.54.0',
  default_options : [
    'warning_level=1',
    'buildtype=debugoptimized',
  ]
)

wireplumber_api_version = '0.4'
wireplumber_so_version = '0'

wireplumber_headers_dir = join_paths(get_option('includedir'), 'wireplumber-' + wireplumber_api_version, 'wp')

if get_option('bindir').startswith('/')
  wireplumber_bin_dir = get_option('bindir')
else
  wireplumber_bin_dir = join_paths(get_option('prefix'), get_option('bindir'))
endif

if get_option('libdir').startswith('/')
  wireplumber_module_dir = join_paths(get_option('libdir'), 'wireplumber-' + wireplumber_api_version)
else
  wireplumber_module_dir = join_paths(get_option('prefix'), get_option('libdir'), 'wireplumber-' + wireplumber_api_version)
endif

if get_option('sysconfdir').startswith('/')
  wireplumber_config_dir = join_paths(get_option('sysconfdir'), 'wireplumber')
else
  wireplumber_config_dir = join_paths(get_option('prefix'), get_option('sysconfdir'), 'wireplumber')
endif

if get_option('datadir').startswith('/')
  wireplumber_data_dir = join_paths(get_option('datadir'), 'wireplumber')
else
  wireplumber_data_dir = join_paths(get_option('prefix'), get_option('datadir'), 'wireplumber')
endif

cc = meson.get_compiler('c')

glib_req_version = '>= 2.58'
glib_dep = dependency('glib-2.0', version : glib_req_version)
gobject_dep = dependency('gobject-2.0', version : glib_req_version)
gmodule_dep = dependency('gmodule-2.0', version : glib_req_version)
gio_dep = dependency('gio-2.0', version : glib_req_version)
giounix_dep = dependency('gio-unix-2.0', version : glib_req_version)
spa_dep = dependency('libspa-0.2', version: '>= 0.2')
pipewire_dep = dependency('libpipewire-0.3', version: '>= 0.3.20')
mathlib = cc.find_library('m')

if get_option('system-lua')
  lua_dep = dependency('lua', version: ['>=5.3.0', '<5.4.0'], required: false)
  if not lua_dep.found()
    lua_dep = dependency('lua5.3', required: false)
    if not lua_dep.found()
      lua_dep = dependency('lua53', required: false)
      if not lua_dep.found()
        lua_dep = dependency('lua-5.3', required: true , allow_fallback: false)
      endif
    endif
  endif
else
  lua_proj = subproject('lua', default_options: ['default_library=static'])
  lua_dep = lua_proj.get_variable('lua_dep')
endif

gnome = import('gnome')
pkgconfig = import('pkgconfig')
gir = find_program('g-ir-scanner', required : get_option('introspection'))
build_gir = gir.found()

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

have_audiofade = cc.compiles('''
  #include <spa/utils/names.h>
  #ifndef SPA_NAME_CONTROL_AUDIO_FADE_SOURCE
  #error "not using the audio fade branch"
  #endif
  int main(void){return 0;}
  ''',
  name: 'audiofade',
  dependencies: pipewire_dep)
if have_audiofade
  add_project_arguments('-DHAVE_AUDIOFADE', language: 'c')
endif

subdir('lib')
subdir('docs')
subdir('modules')
subdir('src')
subdir('tests')
