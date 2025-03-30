executable('wpctl',
  'wpctl.c',
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
)

install_data('shell-completion/wpctl.zsh',
  install_dir: get_option('datadir') / 'zsh/site-functions',
  rename: '_wpctl'
)

executable('wpexec',
  'wpexec.c',
  install: true,
  dependencies : [gobject_dep, gio_dep, wp_dep, pipewire_dep],
)

spa_json_po = files('spa-json-po.py')
