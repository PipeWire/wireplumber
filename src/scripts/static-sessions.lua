-- WirePlumber
--
-- Copyright © 2021 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua
local sessions_config = ...

if sessions_config then
  sessions = {}

  for k, v in pairs(sessions_config) do
    Log.info("Creating session: " .. k)

    sessions[k] = ImplSession()

    if type(v) == "table" then
      v["session.name"] = k
      sessions[k]:update_properties(v)
    else
      sessions[k]:update_properties({
        ["session.name"] = k
      })
    end
    sessions[k]:activate(Features.ALL)
  end
end
