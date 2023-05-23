-- WirePlumber
--
-- Copyright © 2023 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- The main policy script of the "hub policy", It creates and monitors
-- "policy-hub" metadata to figure out which nodes to link. The metadata is to
-- be populated by a wireplumber client. This is unlike the desktop
-- policy(policy-node.lua) which doesnt depend on external entity like this.


linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}

function canLink (si, si_target)
  local si_props = si.properties
  local si_target_props = si_target.properties

  if si_props ["media.type"] ~= si_target_props ["media.type"] then
    Log.info ("linking failure: source and target nodes are NOT of the same media type")
    return false
  end

  if si_props ["item.node.direction"] == si_target_props ["item.node.direction"] then
    Log.info ("linking failure: source and target nodes have incompatible directions")
    return false
  end

  return true
end

function createLink (si, si_target)
  local si_props = si.properties
  local si_target_props = si_target.properties
  local out_item = nil
  local in_item = nil

  Log.info ("linking session items" .. si_props ["node.name"] .. " with " .. si_target_props ["node.name"])

  if si_props ["item.node.direction"] == "output" then
    -- playback
    out_item = si
    in_item = si_target
  else
    -- capture
    in_item = si
    out_item = si_target
  end

  local si_link = SessionItem ("si-standard-link")
  if not si_link:configure {
        ["out.item"] = out_item,
        ["in.item"] = in_item,
        ["out.item.port.context"] = "output",
        ["in.item.port.context"] = "input",
      } then
    Log.info ("linking failure: unable to configure si-standard-link")
    return false
  end

  si_link:register ()

  -- activate
  si_link:activate (Feature.SessionItem.ACTIVE, function(l, e)
    if e then
      Log.info (l, "linking failure: failed to activate si-standard-link: " .. tostring (e))

      l:remove ()
    else
      Log.info (l, "linking success:")
    end
  end)

  return true
end

function link_nodes (source, target)
  local si = nil
  local si_target = nil

  Log.info ("linking node " .. source .. " with " .. target)

  local result = nil

  -- get session items
  si = linkables_om:lookup {
    Constraint { "node.id", "=", source },
  }

  if not si then
    Log.info ("linking failure: not a valid source node id " .. source)
    return
  end

  si_target = linkables_om:lookup {
    Constraint { "node.id", "=", target },
  }

  if not si_target then
    Log.info ("lilinking failure: not a valid target node id " .. target)
    return
  end

  if not canLink (si, si_target) then
    return
  end

  if not createLink (si, si_target) then
    return
  end

end

-- create metadata
policy_hub_metadata = ImplMetadata ("policy-hub")

policy_hub_metadata:activate (Features.ALL, function(m, e)
  if e then
    Log.warning ("failed to activate policy-hub metadata: " .. tostring (e))
    return
  end

  Log.info ("policy-hub metadata created and activated " .. tostring (m))

  -- watch for changes
  m:connect ("changed", function(m, subject, key, type, value)
    link_nodes (key, value)
  end)
end)

linkables_om:activate ()
