libcamera_monitor = {}
libcamera_monitor.properties = {}
libcamera_monitor.rules = {}

function libcamera_monitor.enable()
  if not libcamera_monitor.enabled then
    return
  end

  load_monitor("libcamera", {
    properties = libcamera_monitor.properties,
    rules = libcamera_monitor.rules,
  })
end
