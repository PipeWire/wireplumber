wptoml_lib_sources = [
  'array.cpp',
  'table.cpp',
  'file.cpp',
]

wptoml_lib_headers = [
  'wptoml.h',
  'array.h',
  'table.h',
  'file.h',
]

wptoml_lib = static_library('wptoml-' + wireplumber_api_version,
  wptoml_lib_sources,
  cpp_args : [
    '-D_GNU_SOURCE',
    '-DG_LOG_USE_STRUCTURED',
    '-DG_LOG_DOMAIN="libwptoml"',
  ],
  install: false,
  include_directories: wp_lib_include_dir,
  dependencies : [gobject_dep, cpptoml_dep],
)

wptoml_dep = declare_dependency(
  link_with: wptoml_lib,
  include_directories: wp_lib_include_dir,
  dependencies: [gobject_dep]
)
