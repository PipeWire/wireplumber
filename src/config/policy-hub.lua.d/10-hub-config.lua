hub_policy = {}
hub_policy.enabled = true
hub_policy.policy = {
  -- Set to 'true' to disable channel splitting & merging on nodes and enable
  -- passthrough of audio in the same format as the format of the device.
  -- Note that this breaks JACK support; it is generally not recommended
  ["audio.no-dsp"] = false,
}

hub_policy.hubs = {
  ["playback-hub"] = {
    -- this will be the node.name for both the nodes that will be created.
    ["hub.name"] = "main-hub",
    ["channel.map"] = "FL FR",
    ["capture-props"] = {
      ["media.class"] = "Audio/Sink",
      ["node.name"] = "main-hub",
    },
    ["playback-props"] = {
      ["media.class"] = "Stream/Output/Audio",
      ["node.name"] = "main-hub-output",
    },
  },
}

function hub_policy.enable ()
  if hub_policy.enabled == false then
    return
  end

  -- Session item factories, building blocks for the session management graph
  -- Do not disable these unless you really know what you are doing
  load_module ("si-node")
  load_module ("si-audio-adapter")
  load_module ("si-standard-link")
  load_module ("si-audio-endpoint")

  -- API to access mixer controls, needed for volume ducking
  load_module ("mixer-api")

  -- Create items for nodes that appear in the graph
  load_script ("create-item.lua", hub_policy.policy)

  -- Create hubs as defined in hub_policy.hubs
  load_script ("create-hubs.lua", hub_policy.hubs)

  -- Link nodes to each other to make media flow in the graph
  load_script ("policy-hub.lua", hub_policy.policy)
end
