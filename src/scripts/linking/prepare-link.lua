-- WirePlumber
--
-- Copyright © 2022 Collabora Ltd.
--
-- SPDX-License-Identifier: MIT
--
-- remove the existing link if needed, check the properties of target, which
-- indicate it is not available for linking. If no target is available, send
-- down an error to the corresponding client.

lutils = require ("linking-utils")
cutils = require ("common-utils")
settings = require ("settings-linking")
log = Log.open_topic ("s-linking")

SimpleEventHook {
  name = "linking/prepare-link",
  after = "linking/get-filter-from-target",
  interests = {
    EventInterest {
      Constraint { "event.type", "=", "select-target" },
    },
  },
  execute = function (event)
    local source, _, si, si_props, si_flags, target =
        lutils:unwrap_select_target_event (event)

    local si_id = si.id
    local reconnect = not cutils.parseBool (si_props ["node.dont-reconnect"])
    local exclusive = cutils.parseBool (si_props ["node.exclusive"])
    local si_must_passthrough = cutils.parseBool (si_props ["item.node.encoded-only"])

    log:info (si, string.format ("handling item %d: %s (%s)", si_id,
        tostring (si_props ["node.name"]), tostring (si_props ["node.id"])))

    -- Check if item is linked to proper target, otherwise re-link
    if si_flags.peer_id then
      if target and si_flags.peer_id == target.id then
        log:debug (si, "... already linked to proper target")

        -- Check this also here, in case in default targets changed
        if settings.follow_default_target and si_flags.has_node_defined_target then
          lutils.checkFollowDefault (si, target)
        end

        target = nil
        goto done
      end

      local link = lutils.lookupLink (si_id, si_flags.peer_id)
      if reconnect then
        if link ~= nil then
          -- remove old link
          if ((link:get_active_features () & Feature.SessionItem.ACTIVE) == 0)
          then
            -- remove also not yet activated links: they might never become
            -- active, and we need not wait for it to become active
            log:warning (link, "Link was not activated before removing")
          end
          si_flags.peer_id = nil
          link:remove ()
          log:info (si, "... moving to new target")
        end
      else
        if link ~= nil then
          log:info (si, "... dont-reconnect, not moving")
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
      local target_is_linked, target_is_exclusive = lutils.isLinked (target)
      if target_is_exclusive then
        log:info (si, "... target is linked exclusively")
        target = nil
      end

      if target_is_linked then
        if exclusive or si_must_passthrough then
          log:info (si, "... target is already linked, cannot link exclusively")
          target = nil
        else
          -- disable passthrough, we can live without it
          si_flags.can_passthrough = false
        end
      end
    end

    if not target then
      log:info (si, "... target not found, reconnect:" .. tostring (reconnect))

      local node = si:get_associated_proxy ("node")
      if reconnect and si_flags.was_handled then
        log:info (si, "... waiting reconnect")
        return
      end

      local linger = cutils.parseBool (si_props ["node.linger"])

      if linger then
        log:info (si, "... node linger")
        return
      end

      lutils.sendClientError (event, node,
          reconnect and "no target node available" or "target not found")

      if not reconnect then
        log:info (si, "... destroy node")
        node:request_destroy ()
      end
    end

    ::done::
    event:set_data ("target", target)
  end
}:register ()
