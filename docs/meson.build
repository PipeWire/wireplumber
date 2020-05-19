hotdoc_p = find_program('hotdoc', required: get_option('doc'))
if not hotdoc_p.found()
  message('Hotdoc not found, not building the documentation')
  subdir_done()
endif

hotdoc = import('hotdoc')
required_hotdoc_extensions = ['gi-extension']
foreach extension: required_hotdoc_extensions
  if not hotdoc.has_extensions(extension)
    if get_option('doc').enabled()
      error('Documentation enabled but @0@ missing'.format(extension))
    endif

    message('@0@ extension not found, not building documentation'.format(extension))
    subdir_done()
  endif
endforeach

if not build_gir
  if get_option('doc').enabled()
    error('Documentation enabled but introspection not built.')
  endif

  message('Introspection not built, can\'t build the documentation')
  subdir_done()
endif

wp_doc = hotdoc.generate_doc('wireplumber',
  project_version: wireplumber_api_version,
  sitemap: 'sitemap.txt',
  index: 'index.md',
  gi_index: 'api-reference.md',
  gi_smart_index: true,
  gi_sources: [wp_gir[0].full_path()],
  gi_c_sources: [wp_lib_sources, wp_lib_headers, wpenums_c, wpenums_h],
  gi_c_source_roots: [join_paths(meson.current_source_dir(), '../lib/wp/'), ],
  languages: ['c'],
  dependencies: [wp_dep],
  build_by_default: true,
  install: true,
)
