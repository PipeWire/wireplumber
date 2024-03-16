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
