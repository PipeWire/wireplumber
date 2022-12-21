-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

cutils = require ("common-utils")
config = require ("device-config")

enabled = false

find_echo_cancel_default_node_hook = SimpleEventHook {
  name = "find-echo-cancel-default-node@node",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-default-node" },
      Constraint { "default-node.type", "#", "audio.*" },
    },
  },
  execute = function (event)
    local props = event:get_properties ()
    local available_nodes = event:get_data ("available-nodes")
    local selected_prio = event:get_data ("selected-node-priority") or 0
    local selected_node = event:get_data ("selected-node")

    available_nodes = available_nodes and available_nodes:parse ()
    if not available_nodes then
      return
    end

    -- Get the part after "audio." (= 6 characters)
    local srcsink = props ["default-node.type"]:sub (6)

    for _, node_props in ipairs (available_nodes) do
      if isEchoCancelNode (node_props) then
        local priority = node_props ["priority.session"]
        priority = math.tointeger (priority) or 0
        priority = priority + 10001 - i

        if priority > selected_prio then
          selected_prio = priority
          selected_node = name
        end
      end
    end

    if selected_node then
      event:set_data ("selected-node-priority", selected_prio)
      event:set_data ("selected-node", selected_node)
    end
  end
}

function isEchoCancelNode (node_props, srcsink)
  local virtual = cutils.parseBool (node_props ["node.virtual"])
  return virtual and
      node_props ["node.name"] == config ["echo-cancel-" .. srcsink .. "-name"]
end

function toggleAutoEchoCancel (enable)
  if enable and not enabled then
    find_echo_cancel_default_node_hook:register ()
  elseif not enable and enabled then
    find_echo_cancel_default_node_hook:remove ()
  end
end

config:subscribe ("auto-echo-cancel", toggleAutoEchoCancel)
toggleAutoEchoCancel (config.auto_echo_cancel)
