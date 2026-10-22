-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

log = Log.open_topic ("s-default-streams")

SimpleEventHook {
  name = "default-streams/apply-default-stream",
  after = { "default-streams/find-best-default-stream",
            "default-streams/find-configured-default-stream",
            "default-streams/find-stored-default-stream" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-stream" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local props = event:get_properties ()
    local def_stream_type = props ["default-stream.type"]
    local selected_stream = event:get_data ("selected-stream")

    local om = source:call ("get-object-manager", "metadata")
    local metadata = om:lookup { Constraint { "metadata.name", "=", "default" } }
    if metadata == nil then
      return
    end

    -- the default is given as the node name and port group of the stream
    if selected_stream then
      local key = "default." .. def_stream_type
      local value = {
        ["name"] = selected_stream:get_property ("node.name"),
        ["stream"] = selected_stream:get_port_group (),
      }

      log:info ("set default stream for " .. key .. " " ..
          tostring (value ["name"]) .. " (stream " ..
          tostring (value ["stream"]) .. ")")

      metadata:set (0, key, "Spa:String:JSON", Json.Object (value):to_string ())
    else
      metadata:set (0, "default." .. def_stream_type, nil, nil)
    end
  end
}:register ()
