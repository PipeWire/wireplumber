-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

SimpleEventHook {
  name = "default-nodes/apply-default-node",
  after = { "default-nodes/find-best-default-node",
            "default-nodes/find-echo-cancel-default-node",
            "default-nodes/find-stored-default-node" },
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
    },
  },
  execute = function (event)
    local source = event:get_source ()
    local props = event:get_properties ()
    local def_node_type = props ["default-node.type"]
    local selected_node = event:get_data ("selected-node")

    local om = source:call ("get-object-manager", "metadata")
    local metadata = om:lookup { Constraint { "metadata.name", "=", "default" } }

    if selected_node then
      metadata:set (0, "default." .. def_node_type, "Spa:String:JSON",
                    Json.Object { ["name"] = selected_node }:to_string ())
    else
      metadata:set (0, "default." .. def_node_type, nil, nil)
    end
  end
}:register ()
