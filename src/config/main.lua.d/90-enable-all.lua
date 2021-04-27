-- Provide the "default" pw_metadata, which stores
-- dynamic properties of pipewire objects in RAM
load_module("metadata")

-- Default client access policy
default_access.enable()

-- Load devices
alsa_monitor.enable()
v4l2_monitor.enable()

-- Track/store/restore user choices about devices
device_defaults.enable()

-- Automatically suspends idle nodes after 3 seconds
load_script("suspend-node.lua")

-- Automatically sets device profiles to 'On'
load_module("device-activation")
