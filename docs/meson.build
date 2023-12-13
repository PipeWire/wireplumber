# Find dependencies
pymod = import('python')

python_doc = pymod.find_installation(
  'python3',
  modules: ['sphinx', 'sphinx_rtd_theme', 'breathe', 'sphinx.ext.graphviz'],
  required: get_option('doc')
)
summary({'Python 3 Sphinx related modules': python_doc.found()},
    bool_yn: true,
    section: 'For documentation'
)

python_gir = pymod.find_installation(
  'python3',
  modules: ['lxml'],
  required: get_option('introspection')
)
summary({'Python 3 lxml module': python_gir.found()},
    bool_yn: true,
    section: 'For introspection'
)

if get_option('doc').enabled() or get_option('introspection').enabled()
  doxygen_p = find_program('doxygen', version: '>= 1.8.0', required: true)
elif get_option('doc').auto() or get_option('introspection').auto()
  doxygen_p = find_program('doxygen', version: '>= 1.8.0', required: false)
else
  doxygen_p = disabler()
endif
summary({'Doxygen': doxygen_p.found()}, bool_yn: true, section: 'For introspection')
summary({'Doxygen': doxygen_p.found()}, bool_yn: true, section: 'For documentation')

sphinx_p = find_program('sphinx-build',
    version: '>= 2.1.0', required: get_option('doc'))
summary({'sphinx-build': sphinx_p.found()}, bool_yn: true, section: 'For documentation')
gir_p = find_program('g-ir-scanner', required: get_option('introspection'))
summary({'g-ir-scanner': gir_p.found()}, bool_yn: true, section: 'For introspection')

build_doc = python_doc.found() and doxygen_p.found() and sphinx_p.found()
build_gir = python_gir.found() and doxygen_p.found() and gir_p.found()

# Run doxygen (common for docs and g-i)

if build_doc or build_gir
  doxy_wp_conf_data = configuration_data()
  doxy_wp_conf_data.set('OUTPUT_DIR', meson.current_build_dir() / 'wp')
  doxy_wp_conf_data.set('INPUT', meson.current_source_dir() / '..' / 'lib' / 'wp')
  doxyfile_wp = configure_file(
    input: 'Doxyfile.in',
    output: 'Doxyfile-wp',
    configuration: doxy_wp_conf_data
  )

  doxyxml_wp_depfiles = [wp_lib_sources, wp_lib_headers]
  doxyxml_wp = custom_target('doxyxml_wp',
    command: [doxygen_p, doxyfile_wp],
    depend_files: doxyxml_wp_depfiles,
    output: 'wp',
    build_by_default: true,
  )
endif

# Build documentation

if build_doc
  sphinx_files = files(
    '_static'/'custom.css',
    meson.current_source_dir()/'..'/'README.rst',
    meson.current_source_dir()/'..'/'NEWS.rst',
  )
  sphinx_files += scripts_doc_files
  subdir('rst')

  sphinx_conf_data = configuration_data()
  sphinx_conf_data.set('SRCDIR', meson.current_source_dir())
  sphinx_conf_data.set('OUTDIR', meson.current_build_dir())
  sphinx_conf_data.set('VERSION', meson.project_version())
  sphinx_conf = configure_file(
    input: 'conf.py.in',
    output: 'conf.py',
    configuration: sphinx_conf_data
  )

  custom_target('doc',
    command: [sphinx_p,
      '-q',                       # quiet
      '-E',                       # rebuild from scratch
      '-j', 'auto',               # parallel build
      '-d', '@PRIVATE_DIR@',      # doctrees dir
      '-c', '@OUTDIR@',           # conf.py dir
      '@CURRENT_SOURCE_DIR@/rst', # source dir
      '@OUTPUT@',                 # output dir
    ],
    depend_files: [
      sphinx_conf, sphinx_files,
      doxyfile_wp, doxyxml_wp_depfiles,
    ],
    depends: [doxyxml_wp],
    output: 'html',
    install: true,
    install_dir: wireplumber_doc_dir,
    build_by_default: true,
  )
endif

# Build GObject introspection

if build_gir
  wp_gtkdoc_h = custom_target('wp_gtkdoc_h',
    command: [python_gir,
        '@CURRENT_SOURCE_DIR@/gen-api-gtkdoc.py',
        '-o', '@OUTPUT@',
        '@OUTDIR@/wp/xml',
    ],
    depends: [doxyxml_wp],
    depend_files: [doxyxml_wp_depfiles, 'gen-api-gtkdoc.py'],
    output: 'wp-gtkdoc.h',
    build_by_default: true,
  )

  # A dummy library dependency to force meson to make the gir target
  # depend on wp-gtkdoc.h, because generate_gir() doesn't add dependencies
  # on its sources (meson bug)
  # fixed in 0.59 by https://github.com/mesonbuild/meson/pull/8805
  dummy_c = custom_target('dummy_c',
    command: ['echo', 'int dummy(void) { return 0; }'],
    capture: true,
    output: 'dummy.c',
    build_by_default: true,
  )
  libdummy = library('dummy',
    wp_gtkdoc_h, dummy_c,
    install: false,
    soversion: '0',
    version: '0.0.0',
  )
  dummy_dep = declare_dependency(link_with: libdummy)

  gnome.generate_gir(wp_lib,
    dependencies: [wp_dep, dummy_dep],
    namespace: 'Wp',
    nsversion: wireplumber_api_version,
    export_packages: 'wireplumber-0.4',
    header: 'wp/wp.h',
    sources: [wpenums_h, wp_gtkdoc_h, wp_lib_headers],
    include_directories: [wpenums_include_dir],
    includes: ['GLib-2.0', 'GObject-2.0', 'Gio-2.0'],
    install: true,
  )
endif
