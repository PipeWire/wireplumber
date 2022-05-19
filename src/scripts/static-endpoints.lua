-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua
local endpoints = {}
local endpoint_setting = Settings.get_string ("endpoints")
if endpoint_setting then
  json = Json.Raw (endpoint_setting)
  endpoints = json:parse ()
end

function createEndpoint (factory_name, properties)
  -- create endpoint
  local ep = SessionItem ( factory_name )
  if not ep then
    Log.warning (ep, "could not create endpoint of type " .. factory_name)
    return
  end

  -- configure endpoint
  if not ep:configure(properties) then
    Log.warning(ep, "failed to configure endpoint " .. properties.name)
    return
  end

  -- activate and register endpoint
  ep:activate (Features.ALL, function (item)
    item:register ()
    Log.info(item, "registered endpoint " .. properties.name)
  end)
end


for name, properties in pairs(endpoints) do
  properties["name"] = name
  createEndpoint ("si-audio-endpoint", properties)
end
