device_defaults = {}

device_defaults.properties = {
  -- store preferences to the file system and restore them at startup;
  -- when set to false, default nodes and routes are selected based on
  -- their priorities and any runtime changes do not persist after restart
  ["use-persistent-storage"] = true,
}

function device_defaults.enable()
  -- Selects appropriate default nodes and enables saving and restoring them
  load_module("default-nodes", device_defaults.properties)

  -- Selects appropriate default routes ("ports" in pulseaudio terminology)
  -- and enables saving and restoring them together with
  -- their properties (per-route/port volume levels, channel maps, etc)
  load_script("default-routes.lua", device_defaults.properties)

  if device_defaults.properties["use-persistent-storage"] then
    -- Enables functionality to save and restore default device profiles
    load_module("default-profile")

    -- Save and restore stream-specific properties
    load_script("restore-stream.lua")
  end
end
