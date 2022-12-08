-- WirePlumber

-- Copyright © 2022 Collabora Ltd.
--    @author Ashok Sidipotu <ashok.sidipotu@collabora.com>

-- SPDX-License-Identifier: MIT

-- Script is a Lua Module of common Lua test utility functions

local tutils = {}

function tutils.createNode(name, media_class, factory_name)
  local properties = {}
  properties ["node.name"] = name
  properties ["media.class"] = media_class
  properties ["factory.name"] = factory_name

  node = Node ("adapter", properties)
  node:activate (Feature.Proxy.BOUND, function (n)
    Log.info(node, "created and activated node: "
        .. n.properties ["node.name"])
  end)

  return node
end

tutils.linkables_om = ObjectManager {
  Interest {
    type = "SiLinkable",
    Constraint { "item.factory.name", "c", "si-audio-adapter", "si-node" },
    Constraint { "active-features", "!", 0, type = "gobject" },
  }
}
tutils.linkables_om:activate()

return tutils
