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

gobject_dep = dependency('gobject-2.0')
pipewire_dep = dependency('libpipewire-0.3')

gnome = import('gnome')

subdir('lib')
subdir('src')
