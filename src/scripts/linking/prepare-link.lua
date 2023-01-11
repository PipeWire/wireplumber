-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- remove the existing link if needed, check the properties of target, which
-- indicate it is not available for linking. If no target is available, send
-- down an error to the corresponding client.

local putils = require ("policy-utils")
local cutils = require ("common-utils")

SimpleEventHook {
  name = "linking/prepare-link",
  after = "linking/find-best-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, _, si, si_props, si_flags, target =
        putils:unwrap_find_target_event (event)

    local si_id = si.id
    local reconnect = not cutils.parseBool (si_props ["node.dont-reconnect"])
    local exclusive = cutils.parseBool (si_props ["node.exclusive"])
    local si_must_passthrough = cutils.parseBool (si_props ["item.node.encoded-only"])

    Log.info (si, string.format ("handling item: %s (%s)",
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    -- Check if item is linked to proper target, otherwise re-link
    if si_flags.peer_id then
      if target and si_flags.peer_id == target.id then
        Log.debug (si, "... already linked to proper target")
        -- Check this also here, in case in default targets changed
        putils.checkFollowDefault (si, target,
            si_flags.has_node_defined_target)
        target = nil
        goto done
      end

      local link = putils.lookupLink (si_id, si_flags.peer_id)
      if reconnect then
        if link ~= nil then
          -- remove old link
          if ((link:get_active_features () & Feature.SessionItem.ACTIVE) == 0)
          then
            -- remove also not yet activated links: they might never become
            -- active, and we need not wait for it to become active
            Log.warning (link, "Link was not activated before removing")
          end
          si_flags.peer_id = nil
          link:remove ()
          Log.info (si, "... moving to new target")
        end
      else
        if link ~= nil then
          Log.info (si, "... dont-reconnect, not moving")
          goto done
        end
      end
    end

    -- if the stream has dont-reconnect and was already linked before,
    -- don't link it to a new target
    if not reconnect and si_flags.was_handled then
      target = nil
      goto done
    end

    -- check target's availability
    if target then
      local target_is_linked, target_is_exclusive = putils.isLinked (target)
      if target_is_exclusive then
        Log.info (si, "... target is linked exclusively")
        target = nil
      end

      if target_is_linked then
        if exclusive or si_must_passthrough then
          Log.info (si, "... target is already linked, cannot link exclusively")
          target = nil
        else
          -- disable passthrough, we can live without it
          si_flags.can_passthrough = false
        end
      end
    end

    if not target then
      Log.info (si, "... target not found, reconnect:" .. tostring (reconnect))

      local node = si:get_associated_proxy ("node")
      if not reconnect then
        Log.info (si, "... destroy node")
        node:request_destroy ()
      elseif si_flags.was_handled then
        Log.info (si, "... waiting reconnect")
        return
      end

      local client_id = node.properties ["client.id"]
      if client_id then
        local clients_om = source:call ("get-object-manager", "client")
        local client = clients_om:lookup {
          Constraint { "bound-id", "=", client_id, type = "gobject" }
        }
        if client then
          client:send_error (node ["bound-id"], -2, "no node available")
        end
      end
    end

    ::done::
    event:set_data ("target", target)
  end
}:register ()
