.. _config_access:

Access configuration
====================

WirePlumber includes a "client access" policy which defines access control
rules for PipeWire clients.

Rules
-----

This policy can be configured with rules that can be used to match clients and
apply default permissions to them.

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

Possible permissions are any combination of:

 * ``r``: client is allowed to **read** objects, i.e. "see" them on the registry
   and list their properties
 * ``w``: client is allowed to **write** objects, i.e. call methods that modify
   their state
 * ``x``: client is allowed to **execute** methods on objects; the ``w`` flag
   must also be present to call methods that modify the object
 * ``m``: client is allowed to set **metadata** on objects
 * ``l``: nodes of this client are allowed to **link** to other nodes that the
   client can't "see" (i.e. the client doesn't have ``r`` permission on them)

The special value ``all`` is also supported and it is synonym for ``rwxm``

Permission Managers
-------------------

For more advanced use cases, WirePlumber supports *permission managers* that can
apply per-object permissions dynamically based on rules and object interests.
Permission managers are defined in the ``access.permission-managers`` section
and then referenced by name in ``access.rules``.

Example:

.. code-block::

   access.permission-managers = [
     {
       name = "custom"
       default_permissions = "all"
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
 * ``rules``: a list of match rules with ``set-permissions`` actions that
   grant specific permissions to objects matching the given constraints

When both ``default_permissions`` and ``permission_manager_name`` are set in
a rule's ``update-props`` action, ``default_permissions`` takes precedence and
the permission manager is ignored.
