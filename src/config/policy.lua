-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

target_class_assoc = {
  ["Stream/Input/Audio"] = "Audio/Source",
  ["Stream/Output/Audio"] = "Audio/Sink",
  ["Stream/Input/Video"] = "Video/Source",
  ["Stream/Output/Video"] = "Video/Sink",
}

reverse_direction = {
  ["in"] = "out",
  ["out"] = "in",
}

function find_target(ep, session, endpoints)
  local target = nil

  wp.trace("Searching link target for " .. ep.id ..
      " (name:'" .. ep.name .. "', media_class:'" .. ep.media_class .. "')");

  -- honor node.target, if present
  if ep.properties["node.target"] then
    local id = ep.properties["node.target"]
    for candidate_id, candidate_ep in pairs(endpoints) do
      if candidate_ep.properties["node.id"] == id or
         candidate_id == id
      then
        target = candidate_id
        break
      end
    end
  end

  -- try to find the best target
  if not target then
    local direction = reverse_direction[ep.direction]
    local media_class = target_class_assoc[ep.media_class]
    local highest_prio = -1

    for candidate_id, candidate_ep in pairs(endpoints) do
      if candidate_ep.direction == direction and
         candidate_ep.media_class == media_class
      then
        if candidate_id == session.default_target[direction] then
          target = candidate_id
          wp.debug("choosing default endpoint " .. candidate_id);
          break
        end

        local prio = tonumber(candidate_ep.properties["endpoint.priority"]) or 0
        if highest_prio < prio then
          highest_prio = prio
          target = candidate_id
          wp.debug("considering endpoint " .. candidate_id .. ", priority " .. prio);
        end
      end
    end
  end

  return target
end

function rescan_session(session, endpoints, links)
  -- traverse the endpoints graph and create links where appropriate
  for ep_id, ep in pairs(endpoints) do
    if ep.properties["endpoint.autoconnect"] == "true" or
       ep.properties["endpoint.autoconnect"] == "1"
    then
      local target = nil

      -- check if this endpoint is already linked
      for link_id, link in pairs(links) do
        if link.input_endpoint == ep_id or
           link.output_endpoint == ep_id
        then
          goto skip_endpoint
        end
      end

      -- if not, find a suitable target and link
      target = find_target(ep, session, endpoints)
      if target then
        wp.create_link(ep_id, -1, target, -1)
      end

      ::skip_endpoint::
    end
  end

  -- check all the links to make sure they are active
  for link_id, link in pairs(links) do
    if link.state == "inactive" then
      wp.link_request_state(link_id, "active")
    end
  end
end
