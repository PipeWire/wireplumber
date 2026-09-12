-- Tests that a stream with node.dont-reconnect set is not moved to another
-- target after the target it was linked to disappears, but destroyed with a
-- "target not found" error instead. The link goes away with the target, so the
-- stream has no peers left when the rescan runs.

local tu = require ("test-utils")

Script.async_activation = true

tu.createDeviceNode ("default-device-node", "Audio/Sink")
tu.createDeviceNode ("defined-device-node", "Audio/Sink")

target_removed = false
not_moved = false

-- hook to create stream node, stream is created after the device nodes are
-- ready
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

    if tu.streamsReady () and name ~= "stream-node" then
      tu.createStreamNode ("playback", {
        ["target.object"] = "defined-device-node",
        ["node.dont-reconnect"] = "true",
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

    if stream:get_properties () ["node.name"] ~= "stream-node" then
      return
    end

    if not target_removed then
      if target == nil then
        return
      end

      local link = tu.getStreamLink (stream, target)
      assert (link ~= nil)
      assert (target.properties ["node.name"] == "defined-device-node")
      assert (link:get_active_features () ~= 0)

      -- take the target away, the stream must be destroyed instead of moved
      target_removed = true
      target:get_node ():request_destroy ()
      return
    end

    -- the target is removed asynchronously, a rescan can still see it linked
    if StreamLinkManager.get_peers_count (stream) > 0 then
      return
    end

    assert (target == nil, "the dont-reconnect stream was moved")
    not_moved = true
  end
}:register ()

SimpleEventHook {
  name = "stream-removed@test-linking",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "stream-removed" },
      Constraint { "node.name", "=", "stream-node" },
    },
  },
  execute = function (event)
    assert (not_moved, "the stream was destroyed before losing its target")

    tu.waitUntil (function ()
      return tu.getStreamError ("stream-node") ~= nil
    end, function ()
      assert (tu.getStreamError ("stream-node") == "target not found")
      Script:finish_activation ()
    end)
  end
}:register ()
