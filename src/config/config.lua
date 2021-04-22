components = {}

function load_module(m)
  if not components[m] then
    components[m] = { "libwireplumber-module-" .. m, type = "module" }
  end
end

function load_pw_module(m)
  if not components[m] then
    components[m] = { "libpipewire-module-" .. m, type = "pw_module" }
  end
end

function load_script(s, a)
  if not components[s] then
    components[s] = { s, type = "script/lua", args = a }
  end
end

function load_monitor(s, a)
  load_script("monitors/" .. s .. ".lua", a)
end

function load_access(s, a)
  load_script("access/access-" .. s .. ".lua", a)
end

-- Additional PipeWire modules can be loaded in WirePlumber like this.
-- libpipewire already loads all the modules that we normally need, though.
-- module-spa-node-factory may be needed if you want to use a monitor with
-- LocalNode and the "spa-node-factory" factory
-- ("adapter" is loaded by default, but "spa-node-factory" isn't)
--
-- load_pw_module ("spa-node-factory")

-- Automatically suspends idle nodes after 3 seconds
load_script("suspend-node.lua")

-- Automatically sets device profiles to 'On'
load_module("device-activation")

-- Automatically save and restore default routes
load_module("default-routes")

-- Implements storing metadata about objects in RAM
load_module("metadata")

-- Enables saving and restoring default nodes
load_module("default-nodes")

-- API to access default nodes from scripts
load_module("default-nodes-api")
