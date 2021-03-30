-- Enable local & bluetooth audio devices
alsa_monitor.enable()
bluez_monitor.enable()

-- Enables functionality to save and restore default device profiles
load_module("default-profile")
