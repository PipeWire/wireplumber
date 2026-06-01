-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-monitors-bluez")

AsyncEventHook {
  name = "monitor/bluez/create-node",
  after = "monitor/bluez/create-set-node",
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

        log:info (parent, "Handling node " .. properties["node.name"])

        -- Set sink/source specific properties
        if factory == "api.bluez5.sco.source" or
            (factory == "api.bluez5.a2dp.source" and properties:get_boolean ("api.bluez5.a2dp-duplex")) then
          properties["bluez5.loopback"] = false
          if properties["api.bluez5.profile"] ~= "headset-audio-gateway" then
            properties["api.bluez5.internal"] = true
          end
        end

        log:info("Create node: " .. properties["node.name"] .. ": " .. factory .. " " .. tostring (id))

        -- Create the node
        local node = LocalNode("adapter", properties)
        node:activate(Feature.Proxy.BOUND, function (n, e)
          if e ~= nil then
            transition:return_error ("Failed to activate BT node " ..
                n:get_property ("node.name") .. ": " .. e)
            return
          end

          parent:store_managed_object(id, node)
          transition:advance ()
        end)
      end
    },
  }
}:register ()
