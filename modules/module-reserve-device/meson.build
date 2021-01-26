reserve_device_interface_src = gnome.gdbus_codegen('reserve-device-interface',
  sources: 'org.freedesktop.ReserveDevice1.xml',
  interface_prefix : 'org.freedesktop.ReserveDevice1.',
  namespace : 'Wp'
)

reserve_device_enums = gnome.mkenums_simple('reserve-device-enums',
  sources: [ 'plugin.h', 'reserve-device.h' ],
)

reserve_device_includes = include_directories('.')
