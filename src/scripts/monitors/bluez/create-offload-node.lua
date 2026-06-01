-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

config = {}
config.properties = Conf.get_section_as_properties ("monitor.bluez.properties")

function createOffloadScoNode(parent, id, type, factory, properties)
  local dev_props = parent.properties

  local args = {
    ["audio.channels"] = 1,
    ["audio.position"] = "[MONO]",
  }

  local desc =
      dev_props["device.description"]
      or dev_props["device.name"]
      or dev_props["device.nick"]
      or dev_props["device.alias"]
      or "bluetooth-device"
  -- sanitize description, replace ':' with ' '
  args["node.description"] = desc:gsub("(:)", " ")

  if factory:find("sink") then
    local capture_args = {
      ["device.id"] = parent["bound-id"],
      ["media.class"] = "Audio/Sink",
      ["node.pause-on-idle"] = false,
    }
    for k, v in pairs(properties) do
      capture_args[k] = v
    end

    local name = "bluez_output" .. "." .. (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." .. tostring(id)
    args["node.name"] = name:gsub("([^%w_%-%.])", "_")
    args["capture.props"] = Json.Object(capture_args)
    args["playback.props"] = Json.Object {
      ["node.passive"] = true,
      ["node.pause-on-idle"] = false,
      ["state.restore-props"] = false,
    }
  elseif factory:find("source") then
    local playback_args = {
      ["device.id"] = parent["bound-id"],
      ["media.class"] = "Audio/Source",
      ["node.pause-on-idle"] = false,
    }
    for k, v in pairs(properties) do
      playback_args[k] = v
    end

    local name = "bluez_input" .. "." .. (properties["api.bluez5.address"] or dev_props["device.name"]) .. "." .. tostring(id)
    args["node.name"] = name:gsub("([^%w_%-%.])", "_")
    args["capture.props"] = Json.Object {
      ["node.passive"] = true,
      ["node.pause-on-idle"] = false,
      ["state.restore-props"] = false,
    }
    args["playback.props"] = Json.Object(playback_args)
  else
    log:warning(parent, "Unsupported factory: " .. factory)
    return false
  end

  -- Transform 'args' to a json object here
  local args_json = Json.Object(args)

  -- and get the final JSON as a string from the json object
  local args_string = args_json:get_data()

  local loopback_properties = {}

  local loopback = LocalModule("libpipewire-module-loopback", args_string, loopback_properties)
  parent:store_managed_object(id, loopback)
  return true
end

AsyncEventHook {
  name = "monitor/bluez/create-offload-node",
  after = "monitor/bluez/name-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-bluez-device-node" },
    },
  },
  steps = {
    start = {
      next = "none",
      execute = function (event, transition)
        local properties = event:get_data ("node-properties")
        local parent = event:get_subject ()
        local id = event:get_data ("node-sub-id")
        local type = event:get_data ("type")
        local factory = event:get_data ("factory")
        local node_name = properties["node.name"]

        log:info (parent, "Handling node " .. node_name)

        -- Bypass the hook if offload SCO configuration property is not enabled
        if not config.properties:get_boolean ("bluez5.hw-offload-sco") or
            not factory:find("sco") then
          transition:advance ()
          return
        end

        -- Create the offload SCO loopback nodes module
        if not createOffloadScoNode(parent, id, type, factory, properties) then
          transition:return_error (
              "Failed to create Offload SCO node for BT node " .. node_name)
          event:stop_processing ()
          return
        end

        -- FIXME: We should check and wait for the actual offload loopback nodes
        -- to be created before finishing
        Core.sync (function ()
          log:info (parent, "Created Offload node for BT node " .. node_name)
          transition:advance ()
          event:stop_processing ()
        end)
      end
    },
  }
}:register ()

function setOffloadActive(device, value)
  local pod = Pod.Object {
    "Spa:Pod:Object:Param:Props", "Props", bluetoothOffloadActive = value
  }
  device:set_params("Props", pod)
end

-- Set Offload active hook
SimpleEventHook {
  name = "monitor/bluez/set-offload-active",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "node-state-changed" },
      Constraint { "node.name", "#", "*.bluez_*put*" },
      Constraint { "device.id", "+" },
    },
  },
  execute = function(event)
    local source = event:get_source ()
    local devices_om = source:call ("get-object-manager", "device")
    local node = event:get_subject ()
    local device_id = node.properties:get_int ("device.id")
    local new_state = event:get_properties ()["event.subject.new-state"]

    for d in devices_om:iterate {
      type = "device",
      Constraint { "object.id", "=", device_id}
    } do
      if new_state == "running" then
        setOffloadActive (d, true)
      else
        setOffloadActive (d, false)
      end
    end
  end
}:register ()
