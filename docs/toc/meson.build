# you need to add here any files you add to the toc directory as well
files = [
  'installing-wireplumber.rst',
  'running-wireplumber-daemon.rst',
  'daemon-configuration.rst',
  'daemon-logging.rst',
  'contributing.rst',
  'community.rst',
  'testing.rst',
  'lua_api.rst'
]

foreach file : files
  configure_file(input: file, output: file, copy: true)
endforeach

subdir('lua_api')
