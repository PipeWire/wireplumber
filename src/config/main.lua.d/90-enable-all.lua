-- Provide the "default" pw_metadata, which stores
-- dynamic properties of pipewire objects in RAM
load_module("metadata")

-- Track/store/restore user choices about devices
device_defaults.enable()

-- Track/store/restore user choices about streams
stream_defaults.enable()

-- Link nodes by stream role and device intended role
load_script("intended-roles.lua")

-- Automatically suspends idle nodes after 3 seconds
load_script("suspend-node.lua")
