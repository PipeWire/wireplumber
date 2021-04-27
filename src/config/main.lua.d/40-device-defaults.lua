device_defaults = {}

device_defaults.properties = {
  -- store preferences to the file system and restore them at startup
  ["use-persistent-storage"] = true,
}

function device_defaults.enable()
  -- Enables saving and restoring default nodes
  load_module("default-nodes", device_defaults.properties)

  if device_defaults.properties["use-persistent-storage"] then
    -- Automatically save and restore default routes
    load_module("default-routes")

    -- Enables functionality to save and restore default device profiles
    load_module("default-profile")
  end
end
