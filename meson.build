project('wireplumber', ['c'],
  version : '0.0.1',
  license : 'LGPL',
  meson_version : '>= 0.47.0',
  default_options : [
    'warning_level=1',
    'buildtype=debugoptimized'
  ]
)

wireplumber_api_version = '0.1'
wireplumber_so_version = '0'

modules_install_dir = join_paths(
  get_option('libdir'), 'wireplumber-' + wireplumber_api_version
)

gobject_dep = dependency('gobject-2.0')
gmodule_dep = dependency('gmodule-2.0')
gio_dep = dependency('gio-2.0')
pipewire_dep = dependency('libpipewire-0.3')

gnome = import('gnome')

wp_lib_include_dir = include_directories('lib')

subdir('lib')
subdir('modules')
subdir('src')
