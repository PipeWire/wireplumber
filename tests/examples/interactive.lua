#!/usr/bin/wireplumber -e
--
-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT
--
-- This is an example of an interactive script
--
-- Execute with:
--   wireplumber -e ./interactive.lua option1=value1 option2=value2 ...
-- or:
--   ./interactive.lua option1=value1 option2=value2
-----------------------------------------------------------------------------

--
-- Collects arguments passed in from the command line
-- Assuming option1=value1 option2=value2 were passed, this will be a table
-- like this: { ["option1"] = "value1", ["option2"] = "value2" }
--
local argv = ...

print ("Command-line arguments:")
for k, v in pairs(argv) do
  print ("\t" .. k .. ": " .. v)
end

--
-- Retrieve remote core info
--
local info = Core.get_info()

print ("\nPipeWire daemon info:")
for k, v in pairs(info) do
  if k == "properties" then
    print ("\tproperties:")
    for kk, vv in pairs(v) do
      print ("\t\t" .. kk .. ": " .. vv)
    end
  else
    print ("\t" .. k .. ": " .. v)
  end
end

--
-- Retrieve objects using an ObjectManager
--
-- Note: obj_mgr here cannot be a local variable; we need it to stay alive
-- after the execution has returned to the main loop
--
obj_mgr = ObjectManager {
  Interest { type = "client" },
  Interest { type = "device" },
  Interest { type = "node" },
}

-- Listen for the 'installed' signal from the ObjectManager
-- and execute a function when it is fired
-- This function will be called from the main loop at some point later
obj_mgr:connect("installed", function (om)

  --
  -- Print connected clients
  --
  print ("\nClients:")
  for obj in om:iterate { type = "client" } do

    --
    -- 'bound-id' and 'global-properties' are GObject
    -- properties of WpProxy / WpGlobalProxy
    --
    local id = obj["bound-id"]
    local global_props = obj["global-properties"]

    print ("\t" .. id .. ": " .. global_props["application.name"]
                .. " (PID: " .. global_props["pipewire.sec.pid"] .. ")")
  end

  --
  -- Print devices
  --
  print ("\nDevices:")
  for obj in om:iterate { type = "device" } do
    local id = obj["bound-id"]
    local global_props = obj["global-properties"]

    print ("\t" .. id .. ": " .. global_props["device.name"]
                .. " (" .. global_props["device.description"] .. ")")
  end

  -- Common function to print nodes
  local function printNode(node)
    local id = node["bound-id"]
    local global_props = node["global-properties"]

    -- standard lua string.match() function used here
    if global_props["media.class"]:match("Stream/.*") then
      print ("\t" .. id .. ": " .. global_props["application.name"])
    else
      print ("\t" .. id .. ": " .. global_props["object.path"]
                .. " (" .. global_props["node.description"] .. ")")
    end
  end

  --
  -- Print device sinks
  --
  print ("\nSinks:")
  --
  -- Interest can have additional constraints that can be used to filter
  -- the results. In this case we are only interested in nodes with their
  -- "media.class" global property matching the glob expression "*/Sink"
  --
  local interest = Interest { type = "node",
    Constraint { "media.class", "matches", "*/Sink" }
  }
  for obj in om:iterate(interest) do
    printNode(obj)
  end

  --
  -- Print device sources
  --
  print ("\nSources:")
  local interest = Interest { type = "node",
    Constraint { "media.class", "matches", "*/Source" }
  }
  for obj in om:iterate(interest) do
    printNode(obj)
  end

  --
  -- Print client streams
  --
  print ("\nStreams:")
  local interest = Interest { type = "node",
    Constraint { "media.class", "matches", "Stream/*" }
  }
  for obj in om:iterate(interest) do
    printNode(obj)
  end

  -- Disconnect from pipewire and quit wireplumber
  -- This only works in script interactive mode
  Core.quit()
end)

-- Activate the object manager. This is required to start it,
-- otherwise it doesn't do anything
obj_mgr:activate()
