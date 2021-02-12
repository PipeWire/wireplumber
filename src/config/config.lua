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
  load_script("monitors/monitor-" .. s .. ".lua", a)
end

function load_access(s, a)
  load_script("access/access-" .. s .. ".lua", a)
end

-- Session item factories, building blocks for the session management graph
-- Do not disable these unless you really know what you are doing
load_module("si-adapter")
load_module("si-audio-softdsp-endpoint")
load_module("si-bluez5-endpoint")
load_module("si-convert")
load_module("si-fake-stream")
load_module("si-monitor-endpoint")
load_module("si-simple-node-endpoint")
load_module("si-standard-link")

-- Additional PipeWire modules can be loaded in WirePlumber like this.
-- libpipewire already loads all the modules that we normally need, though.
-- module-spa-node-factory may be needed if you want to use a monitor with
-- LocalNode and the "spa-node-factory" factory
-- ("adapter" is loaded by default, but "spa-node-factory" isn't)
--
-- load_pw_module ("spa-node-factory")

-- Video4Linux2 device management via udev
load_monitor("v4l2")

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

  -- Enables device reservation via org.freedesktop.ReserveDevice1 on D-Bus
  load_module("reserve-device")

  -- ALSA device management via udev
  load_monitor("alsa", {
    use_acp = true,
    use_device_reservation = true,
    enable_midi = true,
    enable_jack_client = false,
  })
end

function enable_endpoints()
  load_script("static-sessions.lua", {
    ["audio"] = {},
    ["video"] = {},
  })
  load_script("create-endpoint.lua")
  load_script("policy-endpoint.lua")
end

enable_access()
enable_endpoints()
