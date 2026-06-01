-- WirePlumber
--
-- Copyright © 2026 Collabora Ltd.
--    @author Julian Bouzas <julian.bouzas@collabora.com>
--
-- SPDX-License-Identifier: MIT

COMBINE_OFFSET = 64

log = Log.open_topic ("s-monitors-bluez")

function createSetNode(parent, id, type, factory, properties)
  local args = {}
  local target_class
  local stream_class
  local rules = {}
  local members_json = Json.Raw (properties["api.bluez5.set.members"])
  local channels_json = Json.Raw (properties["api.bluez5.set.channels"])
  local members = members_json:parse ()
  local channels = channels_json:parse ()

  if properties["media.class"] == "Audio/Sink" then
    args["combine.mode"] = "sink"
    target_class = "Audio/Sink/Internal"
    stream_class = "Stream/Output/Audio/Internal"
  else
    args["combine.mode"] = "source"
    target_class = "Audio/Source/Internal"
    stream_class = "Stream/Input/Audio/Internal"
  end

  log:info("Device set: " .. properties["node.name"])

  for _, member in pairs(members) do
    log:info("Device set member:" .. member["object.path"])
    table.insert(rules,
      Json.Object {
        ["matches"] = Json.Array {
          Json.Object {
            ["object.path"] = member["object.path"],
            ["media.class"] = target_class,
          },
        },
        ["actions"] = Json.Object {
          ["create-stream"] = Json.Object {
            ["media.class"] = stream_class,
            ["audio.position"] = Json.Array (member["channels"]),
            ["state.restore-props"] = false,
          }
        },
      }
    )
  end

  local combine_props = properties:parse ()
  combine_props["node.virtual"] = false
  combine_props["device.api"] = "bluez5"
  combine_props["api.bluez5.set.members"] = nil
  combine_props["api.bluez5.set.channels"] = nil
  combine_props["api.bluez5.set.leader"] = true
  combine_props["audio.position"] = Json.Array (channels)

  args["combine.props"] = Json.Object (combine_props)
  args["stream.props"] = Json.Object {}
  args["stream.rules"] = Json.Array (rules)

  local args_json = Json.Object(args)
  local args_string = args_json:get_data()
  local combine_properties = {}
  log:info("Device set node: " .. args_string)
  return LocalModule("libpipewire-module-combine-stream", args_string, combine_properties)
end

AsyncEventHook {
  name = "monitor/bluez/create-set-node",
  after = "monitor/bluez/create-offload-node",
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

        -- Bypass the hook if 'api.bluez5.set.leader' property is not set
        if properties["api.bluez5.set.leader"] == nil then
          transition:advance ()
          return
        end

        -- Create the combine set node
        local combine = createSetNode(parent, id, type, factory, properties)
        parent:store_managed_object (id + COMBINE_OFFSET, combine)
        parent:set_managed_pending (id)

        -- FIXME: We should check and wait for the actual set node to be created
        -- before finishing
        Core.sync (function ()
          log:info (parent, "Created Set node for BT node " .. node_name)
          transition:advance ()
          event:stop_processing ()
        end)
      end
    },
  }
}:register ()
