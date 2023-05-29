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

  Log.info ("linking session items: " .. si_props ["node.name"] .. " with " .. si_target_props ["node.name"])

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
        ["is.policy.item.link"] = true,
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

function unlink (si, si_target)
  local si_props = si.properties
  local si_target_props = si_target.properties
  local link_found = nil

  Log.info (si, string.format ("unlinking nodes: %s (%s) from %s (%s)",
    tostring (si_props ["node.name"]), tostring (si_props ["node.id"]),
    tostring (si_target_props ["node.name"]), tostring (si_target_props ["node.id"])))

  -- remove any links associated with this item
  for silink in links_om:iterate () do
    local out_id = tonumber (silink.properties ["out.item.id"])
    local in_id = tonumber (silink.properties ["in.item.id"])

    if out_id == si.id or in_id == si_target.id then
      link_found = silink
    elseif out_id == si_target.id or in_id == si.id then
      link_found = silink
    end

  end

  if link_found then
    link_found:remove ()
    Log.info (link_found, "... link removed")
  else
    Log.info ("link not found")
  end
end

function link_unlink_nodes (cmd, source, target)
  local si = nil
  local si_target = nil

  if not target then
    -- here we are assuming one single hub by this name.
    target = "main-hub"
  end

  local result = nil

  -- get session items
  si = linkables_om:lookup {
    Constraint { "node.id", "=", source },
  }

  if not si then
    si = linkables_om:lookup {
      Constraint { "node.name", "=", source },
    }
    if not si then
      Log.info ("link/unlink failure: not a valid source node id/name " .. source)
      return
    end
  end

  si_target = linkables_om:lookup {
    Constraint { "node.id", "=", target },
  }

  if not si_target then
    si_target = linkables_om:lookup {
      Constraint { "node.name", "=", target },
    }
    if not si_target then
      Log.info ("link/unlink failure: not a valid target node id " .. target)
      return
    end
  end

  if cmd == "link" then
    if not canLink (si, si_target) then
      return
    end

    if not createLink (si, si_target) then
      return
    end
  elseif cmd == "unlink" then
    unlink (si, si_target)
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
    k_json = Json.Raw (key)
    k_val = k_json:parse ()

    v_json = Json.Raw (value)
    v_val = v_json:parse ()

    if not v_val.cmd == "link" and not v_val.cmd == "unlink" then
      Log.info ("cmd " .. cmd .. " not supported ")
    end

    link_unlink_nodes (v_val.cmd, k_val ["source.node"], v_val ["target.node"])
  end)
end)

linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}

links_om = ObjectManager {
  Interest {
    type = "SiLink",
    -- only handle links created by this policy
    Constraint { "is.policy.item.link", "=", true },
  }
}

linkables_om:activate ()
links_om:activate ()
