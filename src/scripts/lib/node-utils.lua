-- WirePlumber
--
-- Copyright © 2024 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT

local module = {}

function module.get_session_priority (node_props)
  local priority = node_props ["priority.session"]
  -- fallback to driver priority if session priority is not set
  if not priority then
    priority = node_props ["priority.driver"]
  end
  return math.tointeger (priority) or 0
end

return module
