-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- Settings manager helper
--
-- This sets up a lua table that will automatically populate itself with
-- values from the settings, falling back to the default values specified.
-- Any changes in the settings will automatically change the values of this
-- table.
--
-- Usage:
--
-- local settings_manager = require ("settings-manager")
-- local defaults = {
--   ["foo"] = true,
--   ["foo-bar"] = 3,
-- }
-- local settings = settings_manager.new ("example.prefix.", defaults)
--
-- The above "settings" table should now look like this internally:
-- {
--   foo = true, -- or the value specified in the Settings
--   foo_bar = 3, -- or the value specified in the Settings
-- }
--
-- Additionally, a "subscribe" method is present in the "settings" table, which
-- allows subscribing to changes of the values:
--
-- settings:subscribe ("foo-bar", function (new_value)
--   Log.info ("foo-bar value changed to " .. new_value)
-- end)
--

local settings_manager = {}
local private_api = {}

function private_api.subscribe (self, key, closure)
  if not self.subscribers [key] then
    self.subscribers [key] = {}
  end
  table.insert (self.subscribers [key], closure)
end

function private_api.call_subscribers (self, key, new_value)
  if self.subscribers [key] then
    for i, closure in ipairs (self.subscribers [key]) do
      closure (new_value)
    end
  end
end

function private_api.update_value (self, key, json_value, default)
  local new_value = nil

  if json_value then
    new_value = json_value:parse ()
  else
    new_value = default
  end

  -- only accept values that have the same type as the default value
  if type (new_value) ~= type (default) then
    new_value = default
  end

  -- store only if the new value is not equal to the default
  if new_value ~= default then
    self.values [key] = new_value
  else
    self.values [key] = nil
  end

  return new_value
end

function settings_manager.new (_prefix, _defaults)
  -- private storage table
  local private = {
    prefix = _prefix,
    prefix_len = string.len (_prefix),
    defaults = _defaults,
    values = {},
    subscribers = {},
  }
  setmetatable (private, { __index = private_api })

  -- initialize with the values found in Settings
  for key, default in pairs (private.defaults) do
    local json_value = Settings.get (private.prefix .. key)
    private:update_value (key, json_value, default)
  end

  -- subscribe for changes in Settings
  Settings.subscribe (private.prefix .. "*", function (_, setting, json_value)
    local key = string.sub (setting, private.prefix_len + 1, -1)
    local default = private.defaults [key]

    -- unknown key, ignore it
    if default == nil then
      return
    end

    local new_value = private:update_value (key, json_value, default)
    private:call_subscribers (key, new_value)
  end)

  -- return an empty table with a metatable that will resolve
  -- keys to the values stored in `private` or their default values
  return setmetatable ({}, {
    __private = private,
    __index = function (self, key)
      if key == "subscribe" then
        -- special case, "subscribe" is a method
        return function (self, key, closure)
          local private = getmetatable (self) ["__private"]
          private:subscribe (key, closure)
        end
      else
        local private = getmetatable (self) ["__private"]
        key = string.gsub (key, "_", "-") -- foo_bar_baz -> foo-bar-baz
        local value = private.values [key]

        if (value ~= nil) then
          return value
        else
          return private.defaults [key]
        end
      end
    end,
    __newindex = function (_, key, _)
      error ('Not allowed to modify configuration value')
    end
  })
end

return settings_manager
