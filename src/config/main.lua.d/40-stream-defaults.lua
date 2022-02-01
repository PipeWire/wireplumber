stream_defaults = {}

stream_defaults.properties = {
  -- whether to restore the last stream properties or not
  ["restore-props"] = true,

  -- whether to restore the last stream target or not
  ["restore-target"] = true,
}

function stream_defaults.enable()
  -- Save and restore stream-specific properties
  load_script("restore-stream.lua", stream_defaults.properties)
end
