.. _lua_permissions_api:

Permissions API
===============

This page documents the Lua API for managing PipeWire client permissions: the
``Perm`` constants that name the permission flags, and the ``PermissionManager``
object that computes and maintains per-object permissions for clients.

For the architecture these APIs are part of, see
:ref:`policies_client_access`. For the configuration file equivalents, see
:ref:`config_access`.

Constants
---------

.. describe:: Perm

   PipeWire client permission flags:

   ================== ==============================================
   Constant           Meaning
   ================== ==============================================
   ``Perm.NONE``      no permissions
   ``Perm.R``         read: the object is visible to the client
   ``Perm.W``         write: the client may modify the object
   ``Perm.X``         execute: the client may call methods on it
   ``Perm.M``         metadata: the client may set metadata on it
   ``Perm.L``         link: the client may link to it
   ================== ==============================================

   The combinations ``Perm.RW``, ``Perm.RX``, ``Perm.WX``, ``Perm.RWX``,
   ``Perm.RWXM``, ``Perm.RWXML`` and ``Perm.ALL`` are also provided.
   Note that ``Perm.ALL`` is ``RWXM`` and does *not* include ``L``.

   See :ref:`policies_client_access_model` for what each flag actually allows.

Permission Manager
------------------

The ``PermissionManager`` object manages per-object permissions for clients.
It is created with the global ``PermissionManager()`` constructor and configured
with default permissions, core permissions, and match rules. It must be
activated with ``Features.ALL`` before it can be attached to a client with
:func:`Client.attach_permission_manager`; see
:ref:`policies_client_access_pm` for how the permission list is computed and
when it is refreshed.

.. function:: PermissionManager()

   Creates a new permission manager. Its default permissions start out as
   ``Perm.RWX``.

   :returns: a new permission manager
   :rtype: WpPermissionManager

.. function:: PermissionManager.set_default_permissions(self, perms)

   Binds :c:func:`wp_permission_manager_set_default_permissions`

   Sets the default permissions applied to all objects that don't match any rule.

   :param self: the permission manager
   :param perms: a permission string (e.g. "rx") or an integer bitmask (e.g. ``Perm.RX``)

.. function:: PermissionManager.get_default_permissions(self)

   Binds :c:func:`wp_permission_manager_get_default_permissions`

   Returns the default permissions as an integer bitmask. This can be compared
   against the ``Perm`` constants using bitwise operators.

   **Example:**

   .. code-block:: lua

      local pm = client:get_permission_manager()
      local perms = pm:get_default_permissions()
      if (perms & Perm.RX) == Perm.RX then
        -- client has at least read + execute
      end

   :param self: the permission manager
   :returns: the default permissions bitmask
   :rtype: integer

.. function:: PermissionManager.set_core_permissions(self, perms)

   Binds :c:func:`wp_permission_manager_set_core_permissions`

   Sets the permissions applied specifically to the PipeWire core object (ID 0).
   If not set, the core inherits the default permissions.

   :param self: the permission manager
   :param perms: a permission string or an integer bitmask

.. function:: PermissionManager.add_rules_match(self, rules)

   Binds :c:func:`wp_permission_manager_add_rules_match`

   Adds a set of match rules that grant specific permissions to objects
   matching the given constraints. The rules use the standard
   ``matches``/``actions`` form, with a ``set-permissions`` action whose value
   is a permission string.

   :param self: the permission manager
   :param WpSpaJson rules: a JSON array of match rules
   :returns: the match id (can be used with ``remove_match``)
   :rtype: integer

.. function:: PermissionManager.add_interest_match(self, callback, interest)

   Binds :c:func:`wp_permission_manager_add_interest_match_closure`

   Adds a dynamic match that calls the given callback to determine permissions
   for objects matching the given interest. The callback is called with the
   permission manager, the client whose permissions are being computed, and the
   matched object, and must return a permission bitmask.

   **Example:**

   .. code-block:: lua

      pm:add_interest_match (
        function (_, client, object)
          return client:get_property ("my.sandbox.id") ==
                 object:get_property ("my.sandbox.id") and Perm.ALL or Perm.NONE
        end,
        Interest {
          type = "node",
          Constraint { "media.class", "=", "Audio/Sink" },
        }
      )

   :param self: the permission manager
   :param function callback: a function that returns the permissions for the matched object
   :param WpObjectInterest interest: the interest to match
   :returns: the match id
   :rtype: integer

.. function:: PermissionManager.add_interest_match_simple(self, perms, interest)

   Binds :c:func:`wp_permission_manager_add_interest_match_simple`

   Adds a static match that grants the given permissions to objects matching
   the given interest.

   :param self: the permission manager
   :param integer perms: the permissions bitmask to grant
   :param WpObjectInterest interest: the interest to match
   :returns: the match id
   :rtype: integer

.. function:: PermissionManager.remove_match(self, match_id)

   Binds :c:func:`wp_permission_manager_remove_match`

   Removes a previously added match.

   :param self: the permission manager
   :param integer match_id: the match id returned by an ``add_*_match`` method

.. function:: PermissionManager.update_permissions(self)

   Binds :c:func:`wp_permission_manager_update_permissions`

   Forces a recalculation and update of permissions on all attached clients.
   This is needed when a decision made by an ``add_interest_match`` callback
   depends on external state that has changed, since the permission manager
   only recomputes automatically when objects are added to or removed from the
   graph.

   :param self: the permission manager

Signals
^^^^^^^

.. describe:: client-properties-changed

   Emitted when the properties of an attached client change. The callback
   receives the permission manager and the client.

   **Example:**

   .. code-block:: lua

      pm:connect ("client-properties-changed", function (pm, client)
        if client:get_property ("my.gated") == "true" then
          client:update_permissions { [0] = "rwx" }
        end
      end)

Introspecting a client's permissions
------------------------------------

Scripts that need to know how much a client is trusted before acting on its
behalf can retrieve the permission manager attached to it with
:func:`Client.get_permission_manager`:

.. code-block:: lua

   local client_om = ObjectManager { Interest { type = "client" } }
   client_om:activate()

   -- Check if a client has at least read + execute permissions
   local pm = client:get_permission_manager()
   if pm then
     local perms = pm:get_default_permissions()
     if (perms & Perm.RX) == Perm.RX then
       -- Client has sufficient permissions
     end
   end

Note that this returns ``nil`` for clients whose permissions were set directly
instead of through a permission manager, i.e. those matched by an
``access.rules`` entry that sets ``default_permissions``.
