alsa_monitor = {}
alsa_monitor.properties = {}
alsa_monitor.rules = {}

function alsa_monitor.enable()
  -- The "reserve-device" module needs to be loaded for reservation to work
  if alsa_monitor.properties["alsa.reserve"] then
    load_module("reserve-device")
  end

  load_monitor("alsa", {
    properties = alsa_monitor.properties,
    rules = alsa_monitor.rules,
  })
end
