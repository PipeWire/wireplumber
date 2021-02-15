-- ["<factory-name regex>"] = "<library-name>"
--
-- used to find spa factory names. It maps a spa factory name
-- regular expression to a library name that should contain that factory.
--
spa_libs = {
  ["api.alsa.*"] = "alsa/libspa-alsa",
  ["api.v4l2.*"] = "v4l2/libspa-v4l2",
  ["api.bluez5.*"] = "bluez5/libspa-bluez5",
  ["api.libcamera.*"] = "libcamera/libspa-libcamera",
}

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

function enable_access()
  -- Flatpak access
  load_access("flatpak")

  -- Enables portal permissions via org.freedesktop.impl.portal.PermissionStore
  load_module("portal-permissionstore")

  -- Portal access
  load_access("portal")
end

function enable_audio()
  -- Enables functionality to save and restore default device profiles
  load_module("default-profile")

  -- Enables saving and restoring certain metadata such as default endpoints
  load_module("default-metadata")

  -- Implements storing metadata about objects in RAM
  load_module("metadata")
end

enable_access()
