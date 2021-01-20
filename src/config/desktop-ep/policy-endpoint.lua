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
  ["input"] = "output",
  ["output"] = "input",
}

default_endpoint_key = {
  ["input"] = "default.session.endpoint.sink",
  ["output"] = "default.session.endpoint.source",
}

function findTarget (session, ep)
  local target = nil

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

  return target
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
  local target = findTarget (session, ep)
  if target then
    local ep_is_output = (ep.direction == "output")
    local target_id = target['bound-id']
    local props = {
      ['endpoint-link.output.endpoint'] = (ep_is_output and ep_id) or target_id,
      ['endpoint-link.output.stream'] = -1,
      ['endpoint-link.input.endpoint'] = (ep_is_output and target_id) or ep_id,
      ['endpoint-link.input.stream'] = -1,
    }
    ep:create_link (props)
  end
end

function handleLink (link)
  -- activate all inactive links
  if link:get_state() == "inactive" then
    link:request_state("active")
  end
end

om_metadata = ObjectManager { Interest { type = "metadata" } }
om = ObjectManager { Interest { type = "session" } }

om:connect("object-added", function (om, session)
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

om_metadata:activate()
om:activate()
