-- WirePlumber
--
-- Copyright © 2020 Collabora Ltd.
--    @author George Kiagiadakis <george.kiagiadakis@collabora.com>
--
-- SPDX-License-Identifier: MIT

-- Receive script arguments from config.lua
local config = ...

-- ensure config.move and config.follow are not nil
config.move = config.move or false
config.follow = config.follow or false

target_class_assoc = {
  ["Stream/Input/Audio"] = "Audio/Source",
  ["Stream/Output/Audio"] = "Audio/Sink",
  ["Stream/Input/Video"] = "Video/Source",
  ["Stream/Output/Video"] = "Video/Sink",
}

reverse_direction = {
  ["input"] = "output",
  ["output"] = "input",
}

default_endpoint_key = {
  ["input"] = "default.session.endpoint.sink",
  ["output"] = "default.session.endpoint.source",
}

metadata_key_target_class_assoc = {
  ["default.session.endpoint.sink"] = {
    ["audio"] = "Stream/Output/Audio",
    ["video"] = "Stream/Output/Video",
  },
  ["default.session.endpoint.source"] = {
    ["audio"] = "Stream/Input/Audio",
    ["video"] = "Stream/Input/Video",
  },
}

default_endpoint_target = {
  ["Stream/Input/Audio"] = nil,
  ["Stream/Output/Audio"] = nil
}

-- Endpoint Ids not linked to its node.target prop
auto_linked_endpoints = {}

function createLink (ep, target)
  if ep:get_n_streams() > 0 and target:get_n_streams() > 0 then
    local ep_id = ep['bound-id']
    local target_id = target['bound-id']
    local ep_is_output = (ep.direction == "output")
    local props = {
      ['endpoint-link.output.endpoint'] = (ep_is_output and ep_id) or target_id,
      ['endpoint-link.output.stream'] = -1,
      ['endpoint-link.input.endpoint'] = (ep_is_output and target_id) or ep_id,
      ['endpoint-link.input.stream'] = -1,
    }
    ep:create_link (props)
  end
end

function isEndpointLinkedWith (session, ep, target)
  local ep_id = ep["bound_id"]
  local target_id = target["bound_id"]
  for link in session:iterate_links() do
    local out_ep, _, in_ep, _ = link:get_linked_object_ids()
    if (out_ep == ep_id and in_ep == target_id) or
        (out_ep == target_id and in_ep == ep_id) then
      return true
    end
  end
  return false
end

function moveEndpoint (session, ep, target)
  local ep_id = ep['bound-id']
  local ep_is_output = (ep.direction == "output")
  local total_links = 0
  local moving = false

  -- return if already moved
  if isEndpointLinkedWith(session, ep, target) then
    return
  end

  -- destroy all previous links
  for link in session:iterate_links() do
    local out_ep, _, in_ep, _ = link:get_linked_object_ids()
    if (ep_is_output and out_ep == ep_id) or
        (not ep_is_output and in_ep == ep_id) then
      local curr_target = nil
      total_links = total_links + 1
      -- create new link when all previous links were destroyed
      link:connect ("pw-proxy-destroyed", function (l)
        total_links = total_links - 1
        if total_links == 0 then
          createLink (ep, target)
        end
      end)
      link:request_destroy ()
      moving = true
    end
  end

  -- create link if never linked
  if not moving then
    createLink (ep, target)
  end
end

function moveEndpointFromNodeId (ep_node_id, target_node_id)
  for session in om_session:iterate() do
    local ep = session:lookup_endpoint (Interest {
        type = "endpoint",
        Constraint { "node.id", "=", tostring(ep_node_id), type = "pw" }
    })
    if ep then
      local target = session:lookup_endpoint (Interest {
          type = "endpoint",
          Constraint { "node.id", "=", tostring(target_node_id), type = "pw" }
      })
      if target then
        moveEndpoint (session, ep, target)
        break
      end
    end
  end
end

function reevaluateAutoLinkedEndpoints (ep_media_class, target_id)
  -- make sure the target Id has changed
  if default_endpoint_target[ep_media_class] == target_id then
    return
  end
  default_endpoint_target[ep_media_class] = target_id

  -- move auto linked endpoints to the new target
  for session in om_session:iterate_filtered (Interest { type = "session" } ) do
    local target = session:lookup_endpoint (Interest {
        type = "endpoint",
        Constraint { "bound-id", "=", target_id, type = "gobject" }
    })
    if target then
      for ep in session:iterate_endpoints (Interest{
          type = "endpoint",
          Constraint { "media-class", "=", ep_media_class, type = "gobject" },
      } ) do
        if auto_linked_endpoints[ep["bound-id"]] == true then
          moveEndpoint (session, ep, target)
        end
      end
    end
  end
end

function findTarget (session, ep)
  local target = nil
  local auto_linked = false

  Log.trace(session, "Searching link target for " .. ep['bound-id'] ..
                     " (name:'" .. ep['name'] ..
                     "', media_class:'" .. ep['media-class'] .. "')");

  -- honor node.target, if present
  if ep.properties["node.target"] then
    local id = ep.properties["node.target"]
    for candidate_ep in session:iterate_endpoints() do
      if candidate_ep.properties["node.id"] == id or
         candidate_ep["bound-id"] == id
      then
        target = candidate_ep
        break
      end
    end
  end

  -- try to find the best target
  if not target then
    local metadata = om_metadata:lookup()
    local direction = reverse_direction[ep['direction']]
    local media_class = target_class_assoc[ep['media-class']]
    local highest_prio = -1

    for candidate_ep in session:iterate_endpoints() do
      if candidate_ep['direction'] == direction and
         candidate_ep['media-class'] == media_class
      then
        -- we consider auto linked any target that is not in node.target prop
        auto_linked = true

        -- honor default endpoint, if present
        if metadata then
          local key = default_endpoint_key[direction]
          local value = metadata:find(session['bound-id'], key)
          if candidate_ep['bound-id'] == tonumber(value) then
            target = candidate_ep
            Log.debug(session, "choosing default endpoint " .. target['bound-id']);
            break
          end
        end

        local prio = tonumber(candidate_ep.properties["endpoint.priority"]) or 0
        if highest_prio < prio then
          highest_prio = prio
          target = candidate_ep
          Log.debug(session, "considering endpoint " .. target['bound-id'] ..
                             ", priority " .. prio);
        end
      end
    end
  end

  return target, auto_linked
end

function handleEndpoint (session, ep)
  -- No need to link if autoconnect == false
  local autoconnect = ep.properties['endpoint.autoconnect']
  if autoconnect ~= 'true' and autoconnect ~= '1' then
    return
  end

  local ep_id = ep['bound-id']

  -- check if this endpoint is already linked
  for link in session:iterate_links() do
    local out_ep, _, in_ep, _ = link:get_linked_object_ids()
    if out_ep == ep_id or in_ep == ep_id then
      return
    end
  end

  -- if not, find a suitable target and link
  local target, auto_linked = findTarget (session, ep)
  if target then
    createLink (ep, target)
    auto_linked_endpoints[ep_id] = auto_linked
  end
end

function handleLink (link)
  -- activate all inactive links
  if link:get_state() == "inactive" then
    link:request_state("active")
  end
end

om_metadata = ObjectManager { Interest { type = "metadata" } }
om_session = ObjectManager { Interest { type = "session" } }

om_session:connect("object-added", function (om, session)
  session:connect('endpoints-changed', function (session)
    for ep in session:iterate_endpoints() do
      handleEndpoint(session, ep)
    end
  end)

  session:connect('links-changed', function (session)
    for link in session:iterate_links() do
      handleLink (link)
    end
  end)
end)

om_metadata:connect("object-added", function (om, metadata)
  metadata:connect("changed", function (m, subject, key, t, value)
    if config.move and key == "target.node" then
      moveEndpointFromNodeId (subject, tonumber (value))
    elseif config.follow and string.find(key, "default.session.endpoint") then
      local session = om_session:lookup (Interest {
          type = "session",
          Constraint { "bound-id", "=", subject, type = "gobject" }
      })
      if session then
        local target_class =
            metadata_key_target_class_assoc[key][session.properties["session.name"]]
        if target_class then
          reevaluateAutoLinkedEndpoints (target_class, tonumber (value))
        end
      end
    end
  end)
end)

om_metadata:activate()
om_session:activate()
