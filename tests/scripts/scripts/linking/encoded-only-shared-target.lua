-- Tests that a stream with no raw formats is not linked to a target that
-- another stream is already linked to. Such a stream needs the target
-- configured in passthrough, which would break the existing link.
--
-- The sink is a client stream with media.class set to Audio/Sink, because it
-- has to advertise both a raw and an encoded format and no device node the
-- script tester can make does that.

local tu = require ("test-utils")

Script.async_activation = true

tu.createStreamNode ("dual-capture", {
  ["node.name"] = "dual-sink",
  ["media.class"] = "Audio/Sink",
})

-- hook to create the raw stream, after the sink is ready
SimpleEventHook {
  name = "stream-added@test-linking",
  after = "stream-added@test-utils-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-added" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local name = stream:get_properties () ["node.name"]

    if tu.streamsReady () and name == "dual-sink" then
      assert (stream:get_supported_raw_format_count () > 0)
      assert (stream:get_supported_enc_format_count () > 0)

      tu.createStreamNode ("playback", {
        ["node.name"] = "raw-stream",
        ["target.object"] = "dual-sink",
      })
    end
  end
}:register ()

SimpleEventHook {
  name = "linking/test-linking",
  after = "linking/create-stream-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local stream = event:get_subject ()
    local target = event:get_data ("target")
    local name = stream:get_properties () ["node.name"]

    if name == "raw-stream" then
      if target == nil then
        return
      end

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (target.properties ["node.name"] == "dual-sink")
      assert (link:get_active_features () ~= 0)
      assert (StreamLinkManager.get_peers_count (target) == 1)

      -- create the encoded-only stream only now, so that the sink is already
      -- linked when this one is prepared
      tu.createStreamNode ("encoded-playback", {
        ["node.name"] = "encoded-stream",
        ["target.object"] = "dual-sink",
      })
    elseif name == "encoded-stream" then
      assert (stream:get_supported_raw_format_count () == 0)
      assert (stream:get_supported_enc_format_count () > 0)
      assert (target == nil, "linked to a target that cannot be reconfigured")
      assert (StreamLinkManager.get_peers_count (stream) == 0)

      Script:finish_activation ()
    end
  end
}:register ()
