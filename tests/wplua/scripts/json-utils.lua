rules_json_str = [[
[
  {
    matches = [
      {
        device.name = "~alsa_card.*"
      }
    ]
    actions = {
      update-props = {
        api.alsa.use-acp = true
        api.acp.auto-port = false
      }
    }
  }
  {
    matches = [
      {
        node.name = "alsa_output.0.my-alsa-device"
      }
    ]
    actions = {
      update-props = {
        audio.rate = 96000
        node.description = "My ALSA Node"
        media.class = null
      }
    }
  }
]
]]

match_props = { ["device.name"] = "unmatched-device-name" }
ret, ret_props = JsonUtils.match_rules_update_properties (Json.Raw (rules_json_str), match_props)
assert (ret == 0)
assert (ret_props["device.name"] == match_props["device.name"])

match_props = { ["device.name"] = "alsa_card_0.my-alsa-device" }
ret, ret_props = JsonUtils.match_rules_update_properties (Json.Raw (rules_json_str), match_props)
assert (ret == 2)
assert (ret_props["device.name"] == "alsa_card_0.my-alsa-device")
assert (ret_props["api.alsa.use-acp"] == "true")
assert (ret_props["api.acp.auto-port"] == "false")

match_props = { ["node.name"] = "alsa_output.0.my-alsa-device" }
ret, ret_props = JsonUtils.match_rules_update_properties (Json.Raw (rules_json_str), match_props)
assert (ret == 2)
assert (ret_props["node.name"] == "alsa_output.0.my-alsa-device")
assert (ret_props["audio.rate"] == "96000")
assert (ret_props["node.description"] == "My ALSA Node")
assert (ret_props["media.class"] == nil)

match_props = {
  ["node.name"] = "alsa_output.0.my-alsa-device",
  ["media.class"] = "Audio/Sink",
  ["audio.rate"] = "48000",
  ["node.description"] = "Test",
}
ret, ret_props = JsonUtils.match_rules_update_properties (Json.Raw (rules_json_str), match_props)
assert (ret == 3)
assert (ret_props["node.name"] == "alsa_output.0.my-alsa-device")
assert (ret_props["audio.rate"] == "96000")
assert (ret_props["node.description"] == "My ALSA Node")
assert (ret_props["media.class"] == nil)

rules_json_str = [[
[
  {
    matches = [
      {
        device.name = "~alsa_card.*"
      }
    ]
    actions = {
      update-props = {
        api.acp.auto-port = false
      }
      set-answer = 42
    }
  }
  {
    matches = [
      {
        test.error = true
      }
    ]
    actions = {
      generate-error = "test.error is true"
    }
  }
  {
    matches = [
      {
        device.name = "alsa_card.1"
      }
    ]
    actions = {
      set-description = "My ALSA Device"
    }
  }
]
]]

function match_rules_callback (action, value)
  if action == "update-props" then
    local updates = value:parse ()
    for k,v in pairs (updates) do
      match_props[k] = tostring (v)
    end
  elseif action == "set-answer" then
    local v = value:parse ()
    match_props["answer.universe"] = tostring (v)
  elseif action == "generate-error" then
    local err = value:parse ()
    return false, tostring (err)
  elseif action == "set-description" then
    local str = value:parse ()
    match_props["device.description"] = tostring (str)
  end

  return true
end

match_props = {
  ["device.name"] = "alsa_card.1",
  ["test.error"] = "false",
}
ret, err = JsonUtils.match_rules (Json.Raw (rules_json_str), match_props, match_rules_callback)
assert (ret == true)
assert (err == nil)
assert (match_props["device.name"] == "alsa_card.1")
assert (match_props["api.acp.auto-port"] == "false")
assert (match_props["answer.universe"] == "42")
assert (match_props["device.description"] == "My ALSA Device")

match_props = {
  ["device.name"] = "alsa_card.1",
  ["test.error"] = "true",
}
ret, err = JsonUtils.match_rules (Json.Raw (rules_json_str), match_props, match_rules_callback)
assert (ret == false)
assert (err == "test.error is true")
assert (match_props["device.name"] == "alsa_card.1")
assert (match_props["api.acp.auto-port"] == "false")
assert (match_props["answer.universe"] == "42")
assert (match_props["device.description"] == nil)
