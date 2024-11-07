-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- Based on https://github.com/kikito/sandbox.lua
-- Copyright © 2013 Enrique García Cota
--
-- SPDX-License-Identifier: MIT

local SANDBOX_CONFIG = ...
local SANDBOX_ENV = {}

function create_sandbox_env()
  local function populate_env(id)
    local module, method = id:match('([^%.]+)%.([^%.]+)')
    if module then
      SANDBOX_ENV[module]         = SANDBOX_ENV[module] or {}
      SANDBOX_ENV[module][method] = _G[module][method]
    else
      SANDBOX_ENV[id] = _G[id]
    end
  end

  if not SANDBOX_ENV._VERSION then
    -- List of exported functions and packages
    ([[ _VERSION assert error ipairs   next pairs  tonumber
        pcall    select print tostring type xpcall require
        table    string math  package  utf8 debug  coroutine
        os.clock  os.difftime os.time  os.date     os.getenv
        setmetatable getmetatable
    ]]):gsub('%S+', populate_env)

    -- Additionally export everything in SANDBOX_EXPORT
    if type(SANDBOX_EXPORT) == "table" then
      for k, v in pairs(SANDBOX_EXPORT) do
        SANDBOX_ENV[k] = v
      end
    end

    -- Additionally protect packages from malicious scripts trying to override methods
    for k, v in pairs(SANDBOX_ENV) do
      if type(v) == "table" then
        SANDBOX_ENV[k] = setmetatable({}, {
          __index = v,
          __call = function(t, ...)
            return t["__new"](...)
          end,
          __newindex = function(_, attr_name, _)
            error('Can not modify ' .. k .. '.' .. attr_name .. '. Protected by the sandbox.')
          end
        })
      end
    end
  end

  -- chunk's environment will be an empty table with __index
  -- to access our SANDBOX_ENV (without being able to write it)
  return setmetatable({}, {
    __index = SANDBOX_ENV,
  })
end

if SANDBOX_CONFIG["isolate_env"] then
  -- in isolate_env mode, use a separate environment for each loaded chunk and
  -- store all of them in a global table so that they are not garbage collected
  SANDBOX_ENV_LIST = {}

  function sandbox(chunk, ...)
    local env = create_sandbox_env()
    -- store the chunk's environment so that it is not garbage collected
    table.insert(SANDBOX_ENV_LIST, env)
    -- set it as the chunk's 1st upvalue (__ENV)
    debug.setupvalue(chunk, 1, env)
    -- execute the chunk
    return chunk(...)
  end
else
  -- in common_env mode, use the same environment for all loaded chunks
  -- chunk's environment will be an empty table with __index
  -- to access our SANDBOX_ENV (without being able to write it)
  SANDBOX_COMMON_ENV = create_sandbox_env()

  function sandbox(chunk, ...)
    -- set it as the chunk's 1st upvalue (__ENV)
    debug.setupvalue(chunk, 1, SANDBOX_COMMON_ENV)
    -- execute the chunk
    return chunk(...)
  end
end
