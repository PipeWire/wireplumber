.. _config_access:

Access configuration
====================

WirePlumber includes a "client access" policy which defines access control
rules for PipeWire clients. This page documents the configuration sections that
the policy reads; for how the policy itself works, see
:ref:`policies_client_access`.

Permission strings
------------------

Permissions are written as chmod-like strings, e.g. ``"rwx"`` or ``"r-xm"``,
built out of any combination of:

 * ``r``: client is allowed to **read** objects, i.e. "see" them on the registry
   and list their properties
 * ``w``: client is allowed to **write** objects, i.e. call methods that modify
   their state
 * ``x``: client is allowed to **execute** methods on objects; the ``w`` flag
   must also be present to call methods that modify the object
 * ``m``: client is allowed to set **metadata** on objects
 * ``l``: nodes of this client are allowed to **link** to other nodes that the
   client can't "see" (i.e. the client doesn't have ``r`` permission on them)

The ``-`` character is ignored and only serves to make the string more readable
when a flag is omitted. The special value ``all`` is also supported and is a
synonym for ``rwxm``; note that it does not include ``l``.

.. _config_access_rules:

access.rules
------------

``access.rules`` matches clients and applies default permissions to them. In
addition to the client's own properties, the constraints can match on
``access``, which is the client's access category as derived from the
``pipewire.access``, ``pipewire.client.access`` and ``pipewire.sec.flatpak``
properties.

Example:

.. code-block::

   access.rules = [
     {
       matches = [
         {
           access = "flatpak"
           media.category = "Manager"
         }
       ]
       actions = {
         update-props = {
           access = "flatpak-manager"
           default_permissions = "all",
         }
       }
     }
     {
       matches = [
         {
           access = "flatpak"
         }
       ]
       actions = {
         update-props = {
           default_permissions = "rx"
         }
       }
     }
   ]

The ``update-props`` action understands the following properties:

 * ``access``: the effective access category to record for the client. It is
   published on the client as the ``pipewire.access.effective`` property.
 * ``default_permissions``: permissions to apply directly to the client as its
   fallback for all objects.
 * ``permission_manager_name``: the name of a permission manager declared in
   ``access.permission-managers`` to attach to the client.

When both ``default_permissions`` and ``permission_manager_name`` are set,
``default_permissions`` takes precedence and the permission manager is ignored.

.. _config_access_permission_managers:

access.permission-managers
--------------------------

For more advanced use cases, WirePlumber supports *permission managers* that can
apply per-object permissions dynamically based on rules and object interests.
Permission managers are defined in the ``access.permission-managers`` section
and then referenced by name from ``access.rules``.

Example:

.. code-block::

   access.permission-managers = [
     {
       name = "custom"
       default_permissions = "all"
       core_permissions = "rx"
       rules = [
         {
           matches = [
             {
               media.class = "Audio/Source"
             }
           ]
           actions = {
             set-permissions = "-"
           }
         }
       ]
     }
   ]

   access.rules = [
     {
       matches = [
         {
           application.name = "paplay"
         }
       ]
       actions = {
         update-props = {
           permission_manager_name = "custom"
         }
       }
     }
   ]

Each permission manager supports the following properties:

 * ``name``: (required) a unique name used to reference the manager from
   ``access.rules``
 * ``default_permissions``: the fallback permissions applied to all objects
   that don't match any rule (applied as ``PW_ID_ANY``)
 * ``core_permissions``: permissions applied specifically to the PipeWire core
   object (``PW_ID_CORE``, ID 0). This is useful when you want to allow a
   client to interact with the core (e.g. enumerate objects, subscribe to
   events) while restricting access to individual objects. If not set, the
   ``default_permissions`` value is used for the core as well.
 * ``rules``: a list of match rules with ``set-permissions`` actions that
   grant specific permissions to objects matching the given constraints

Permissions from all matching rules are combined, and objects that match no
rule fall back to ``default_permissions``. See
:ref:`policies_client_access_pm` for the details.

Going further
-------------

Decisions that cannot be expressed as rules require a script. See
:ref:`policies_client_access` for how to hook into the policy and
:ref:`lua_permissions_api` for the permission manager Lua API.
