bluez_monitor = {}
bluez_monitor.properties = {}
bluez_monitor.rules = {}

function bluez_monitor.enable()
  load_monitor("bluez", {
    properties = bluez_monitor.properties,
    rules = bluez_monitor.rules,
  })

  load_optional_module("logind")
end
