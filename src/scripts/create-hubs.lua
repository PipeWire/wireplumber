-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT
local hub_config = ... or {}

-- create hubs in global scope so that the hubs are available through out.
hubs = {}

function createHubs ()
  for hub, config in pairs (hub_config) do
    local hub_args = {}
    hub_args ["node.name"] = config ["hub.name"]
    hub_args ["channel.map"] = config ["channel.map"]
    hub_args ["capture.props"] = Json.Object {
      ["media.class"] = config ["capture-props"] ["media.class"],
    }
    hub_args ["playback.props"] = Json.Object {
      ["media.class"] = config ["playback-props"] ["media.class"],
    }

    -- Transform 'args' to a json object here
    local args_json = Json.Object (hub_args)

    -- and get the final JSON as a string from the json object
    local hub_args_s = args_json:get_data ()

    Log.info ("creating hub " .. config ["hub.name"] .. " with args " .. hub_args_s)
    hubs [config ["hub.name"]] = LocalModule ("libpipewire-module-loopback", hub_args_s)
  end
end

-- createHubs () is supposed to be called during the bootup, this makes sure it
-- is called after all the previous plugins are activated.
Core.idle_add (function()
  createHubs ()
  return false
end)
