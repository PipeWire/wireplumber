# you need to add here any files you add to the toc directory as well
files = [
  'lua_core_api.rst',
  'lua_client_api.rst',
  'lua_endpoint_api.rst',
  'lua_global_proxy_api.rst',
  'lua_metadata_api.rst',
  'lua_node_api.rst',
  'lua_object_api.rst',
  'lua_object_interest_api.rst',
  'lua_object_manager_api.rst',
  'lua_pipewire_object_api.rst',
  'lua_proxy_api.rst',
  'lua_session_bin_api.rst',
  'lua_session_item_api.rst',
  'lua_source_api.rst',
  'lua_spa_device_api.rst',
]

foreach file : files
  configure_file(input: file, output: file, copy: true)
endforeach
