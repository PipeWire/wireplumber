i18n = import('i18n')

pymod = import('python')
python_po = pymod.find_installation('python3')
spa_json_dump_po = find_program('spa-json-dump', required: false)

if python_po.found() and spa_json_dump_po.found()
  conf_pot = custom_target('conf.pot',
    input : i18n_conf,
    output : 'conf.pot',
    build_by_default : true,
    command : [
      python_po, spa_json_po, '--spa-json-dump=' + spa_json_dump_po.full_path(),
      '-k', '/wireplumber.settings.schema/[^/]*/description$',
      '-o', '@CURRENT_SOURCE_DIR@/conf.pot',
      '@INPUT@',
    ],
    capture: true,
  )
endif

i18n.gettext(
  meson.project_name(),
  preset: 'glib',
  # Page width is set to 90 characters in order to avoid bad wrapping of the
  # bug reporting address.
  args: ['--msgid-bugs-address=https://gitlab.freedesktop.org/pipewire/wireplumber/issues/new',
         '--width=90', '--keyword=I18n.gettext:1', '--keyword=I18n.ngettext:1,2']
)

po_dir = meson.current_source_dir()
