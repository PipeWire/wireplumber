project('wireplumber', ['c', 'cpp'],
  version : '0.1.90',
  license : 'MIT',
  meson_version : '>= 0.51.0',
  default_options : [
    'warning_level=1',
    'buildtype=debugoptimized',
    'cpp_std=c++11',
  ]
)

wireplumber_api_version = '0.1'
wireplumber_so_version = '0'

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

cmake = import('cmake')
cpptoml = cmake.subproject('cpptoml')
cpptoml_dep = cpptoml.dependency('cpptoml')

gobject_dep = dependency('gobject-2.0')
gmodule_dep = dependency('gmodule-2.0')
gio_dep = dependency('gio-2.0')
pipewire_dep = dependency('libpipewire-0.3')

gnome = import('gnome')
pkgconfig = import('pkgconfig')
gir = find_program('g-ir-scanner', required : get_option('introspection'))
build_gir = gir.found()

wp_lib_include_dir = include_directories('lib')

cc = meson.get_compiler('c')
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

ccpp = meson.get_compiler('cpp')
if ccpp.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'cpp')
endif

subdir('lib')
subdir('docs')
subdir('modules')
subdir('src')
subdir('tests')
subdir('tools')
