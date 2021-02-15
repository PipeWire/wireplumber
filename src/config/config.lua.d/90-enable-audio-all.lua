-- Enable local & bluetooth audio devices
alsa_monitor.enable()
bluez_monitor.enable()

-- Enables functionality to save and restore default device profiles
load_module("default-profile")

-- Enables saving and restoring certain metadata such as default endpoints
load_module("default-metadata")

-- Implements storing metadata about objects in RAM
load_module("metadata")
