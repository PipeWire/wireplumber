-- WirePlumber
--
-- Copyright © 2021 Asymptotic
--    @author Arun Raghavan <arun@asymptotic.io>
--
-- SPDX-License-Identifier: MIT
--
-- Route streams of a given role (media.role property) to devices that are
-- intended for that role (device.intended-roles property)

metadata_om = ObjectManager {
  Interest {
    type = "metadata",
    Constraint { "metadata.name", "=", "default" },
  }
}

devices_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
    Constraint { "device.intended-roles", "is-present", type = "pw" },
  }
}

streams_om = ObjectManager {
  Interest {
    type = "node",
    Constraint { "media.class", "matches", "Stream/*/Audio", type = "pw-global" },
    Constraint { "media.role", "is-present", type = "pw-global" }
  }
}

streams_om:connect("object-added", function (streams_om, stream)
  local stream_role = stream.properties["media.role"]
  local is_input = stream.properties["media.class"]:find("Input") ~= nil

  for dev in devices_om:iterate() do
    local is_source = dev.properties["media.class"]:find("Source") ~= nil
    local dev_roles = dev.properties["device.intended-roles"]

    -- Make sure the stream and device direction match
    if is_input ~= is_source then
      goto next
    end

    for role in dev_roles:gmatch("(%a+)") do
      if role == stream_role then
        Log.info(stream,
          string.format("Routing stream '%s' (%d) with role '%s' to '%s' (%d)",
            stream.properties["node.name"], stream["bound-id"], stream_role,
            dev.properties["node.name"], dev["bound-id"])
        )

        local metadata = metadata_om:lookup()
        metadata:set(stream["bound-id"], "target.node", "Spa:Id", dev["bound-id"])
      end
    end

    ::next::
  end
end)

metadata_om:activate()
devices_om:activate()
streams_om:activate()
