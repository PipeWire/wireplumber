-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

mutils = require ("monitor-utils")

log = Log.open_topic ("s-monitors-bluez")

config = {}
config.rules = Conf.get_section_as_json ("monitor.bluez.rules", Json.Array {})

SimpleEventHook {
  name = "monitor/bluez/name-node",
  before = "monitor/bluez/create-offload-node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "create-bluez-device-node" },
    },
  },
  execute = function(event)
    local properties = event:get_data ("node-properties")
    local parent = event:get_subject ()
    local parent_id = parent["bound-id"]
    local dev_props = parent.properties
    local factory = event:get_data ("factory")
    local id = event:get_data ("node-sub-id")

    log:info (parent, "Handling node " .. tostring (id))

    -- set the device id and spa factory name; REQUIRED, do not change
    properties["device.id"] = parent_id
    properties["factory.name"] = factory
    properties["spa.object.id"] = id

    -- set the default pause-on-idle setting
    properties["node.pause-on-idle"] = false

    -- set the node description
    local desc =
        dev_props["device.description"]
        or dev_props["device.name"]
        or dev_props["device.nick"]
        or dev_props["device.alias"]
        or "bluetooth-device"
    -- sanitize description, replace ':' with ' '
    properties["node.description"] = desc:gsub("(:)", " ")

    -- set the node name
    local name_prefix = ((factory:find("sink") and "bluez_output") or
         (factory:find("source") and "bluez_input" or factory))
    properties["node.name"] = mutils.get_bluez_node_name (name_prefix,
        properties["api.bluez5.address"], dev_props["device.name"], id)

    -- set priority
    if not properties["priority.driver"] then
      local priority = factory:find("source") and 2010 or 1010
      properties["priority.driver"] = priority
      properties["priority.session"] = priority
    end

    -- autoconnect if it's a stream
    if properties["api.bluez5.profile"] == "headset-audio-gateway" or
       properties["api.bluez5.profile"] == "bap-sink" or
       factory:find("a2dp.source") or factory:find("media.source") then
      properties["node.autoconnect"] = true
    end

    -- apply properties from the rules in the configuration file
    properties = JsonUtils.match_rules_update_properties (config.rules, properties)

    event:set_data ("node-properties", properties)
  end
}:register ()
