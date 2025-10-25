-- WirePlumber
--
-- Copyright © 2025 Phosh.mobi e.V.
--
-- SPDX-License-Identifier: MIT
--
-- Pick up a preferred target node for the output stream of role-based loopbacks

lutils = require ("linking-utils")
cutils = require ("common-utils")

log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/find-media-role-sink-target",
  after = { "linking/find-defined-target",
	    "linking/find-media-role-target" },
  before = "linking/prepare-link",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local _, om, si, si_props, _, target =
       lutils:unwrap_select_target_event (event)

    local node_name = si_props["node.name"]
    local target_direction = cutils.getTargetDirection (si_props)
    local media_class = si_props["media.class"]
    local link_group = si_props["node.link-group"]
    local is_virtual = si_props["node.virtual"]

    log:info (si, string.format ("Lookup for '%s' (%s) / '%s' / '%s'",
				 node_name, tostring (si_props ["node.id"]), media_class, link_group))

    --- bypass the hook if the target is already set or there's no link group
    if target or media_class ~= "Stream/Output/Audio" or not is_virtual or link_group == nil then
      return
    end

    --- We link the output node but the relevant properties are on the input node
    --- of the link group
    local input_node = om:lookup {
      type = "SiLinkable",
      Constraint { "media.class", "=", "Audio/Sink" },
      Constraint { "node.link-group", "=", link_group },
    }

    if input_node == nil then
       log:warning (si, string.format("No input node for %s found", link_group))
       return
    end

    local target_name = input_node.properties["policy.role-based.preferred-target"]
    --- no preferred target
    if target_name == nil then
       return
    end

    local si_target = om:lookup {
      type = "SiLinkable",
      Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
      Constraint { "node.name", "=", target_name },
    }
    if si_target == nil then
       si_target = om:lookup {
	  type = "SiLinkable",
	  Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
	  Constraint { "node.nick", "=", target_name },
       }
    end
    if si_target then
       log:info (si,
        string.format ("... role based sink target picked: %s (%s)",
          tostring (si_target.properties ["node.name"]),
          tostring (si_target.properties ["node.id"])))
       event:set_data ("target", si_target)
    end
  end
}:register ()
