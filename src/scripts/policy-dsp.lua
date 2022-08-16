-- WirePlumber
--
-- Copyright © 2022-2023 The WirePlumber project contributors
--    @author Dmitry Sharshakov <d3dx12.xx@gmail.com>
--
-- SPDX-License-Identifier: MIT

local config = ... or {}
config.properties = config.properties or {}
config.rules = config.rules or {}

for _, r in ipairs(config.rules) do
  r.interests = {}
  for _, i in ipairs(r.matches) do
    local interest_desc = { type = "properties" }

    for _, c in ipairs(i) do
      c.type = "pw"
      table.insert(interest_desc, Constraint(c))
    end

    local interest = Interest(interest_desc)
    table.insert(r.interests, interest)
  end

  if r.device_matches then
    r.device_interests = {}
    for _, i in ipairs(r.device_matches) do
      local interest_desc = { type = "properties" }

      for _, c in ipairs(i) do
        c.type = "pw"
        table.insert(interest_desc, Constraint(c))
      end

      local interest = Interest(interest_desc)
      table.insert(r.device_interests, interest)
    end
  end
end

-- Look up device which owns the sink (for profile switching)
devices_om = ObjectManager {
  Interest { type = "device" }
}

-- TODO: only check for hotplug of devices with known DSP rules
nodes_om = ObjectManager {
  Interest { type = "node" },
}

filter_chains = {}

-- Check if the device matches any of the interests
function checkDevice (device, device_interests)
  for _, interest in ipairs(device_interests) do
    if interest:matches(device["global-properties"]) then
      return true
    end
  end
  return false
end

nodes_om:connect("object-added", function (om, node)
  for _, r in ipairs(config.rules or {}) do
    for _, interest in ipairs(r.interests) do
      if interest:matches(node["global-properties"]) then
        local id = node["global-properties"]["object.id"]

        local device = devices_om:lookup(Interest {
          type = "device",
          Constraint { "object.id", "=", node["global-properties"]["device.id"] }
        })

        if r.device_interests and not checkDevice(device, r.device_interests) then
          -- This node belongs to another device rather than the specified one
          return
        end

        if r.properties and r.properties.profile then
          local index = nil
          for profile in device:iterate_params("EnumProfile") do
            local p = profile:parse()
            if p.properties.name == r.properties.profile then
              local pod = Pod.Object {
                "Spa:Pod:Object:Param:Profile", "Profile",
                index = p.properties.index
              }
              device:set_param("Profile", pod)

              break
            end
          end
        end

        if r.filter_chain then
          if filter_chains[id] then
            Log.warning("Sink " .. id .. " has been plugged now, but has a filter chain loaded. Skipping")
          else
            filter_chains[id] = LocalModule("libpipewire-module-filter-chain", r.filter_chain, {})
          end
        end
      end
    end
  end
end)

nodes_om:connect("object-removed", function (om, node)
  local id = node["global-properties"]["object.id"]
  if filter_chains[id] then
    Log.debug("Unloading filter chain associated with sink " .. id)
    filter_chains[id] = nil
  else
    Log.debug("Disconnected sink " .. id .. " does not have any filters to be removed")
  end
end)

devices_om:activate()
nodes_om:activate()
