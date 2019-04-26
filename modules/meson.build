common_c_args = [
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED',
]

shared_library(
  'wireplumber-module-default-session',
  ['module-default-session.c'],
  c_args : [common_c_args, '-DG_LOG_DOMAIN="wireplumber-module-default-session"'],
  install : true,
  #install_dir : modules_install_dir,
  dependencies : [wp_dep, pipewire_dep],
)
