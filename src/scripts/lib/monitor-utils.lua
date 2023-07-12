-- WirePlumber

-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of monitor Lua utility functions

local mutils = {}

-- finds out if any of the managed objects(nodes of a device or devices of
-- device enumerator) has duplicate values
function mutils.findDuplicate (parent, id, property, value)
  for i = 0, id - 1, 1 do
    local obj = parent:get_managed_object (i)
    if obj and obj.properties[property] == value then
      return true
    end
  end
  return false
end

return mutils
