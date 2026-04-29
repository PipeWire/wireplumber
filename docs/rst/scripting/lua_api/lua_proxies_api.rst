 .. _lua_proxies_api:

PipeWire Proxies
================

Proxy
.....

Lua objects that bind a :ref:`WpProxy <proxy_api>` contain the following methods:

.. function:: Proxy.get_interface_type(self)

   Binds :c:func:`wp_proxy_get_interface_type`

   :param self: the proxy
   :returns: the proxy type, the proxy type version
   :rtype: string, integer

PipeWire Object
...............

Lua objects that bind a :ref:`WpPipewireObject <pipewire_object_api>`
contain the following methods:

.. function:: PipewireObject.iterate_params(self, param_name)

   Binds :c:func:`wp_pipewire_object_enum_params_sync`

   :param self: the proxy
   :param string param_name: the PipeWire param name to enumerate,
                             ex "Props", "Route"
   :returns: the available parameters
   :rtype: Iterator; the iteration items are Spa Pod objects

.. function:: PipewireObject.set_param(self, param_name, pod)

   Binds :c:func:`wp_pipewire_object_set_param`

   :param self: the proxy
   :param string param_name: The PipeWire param name to set, ex "Props", "Route"
   :param Pod pod: A Spa Pod object containing the new params

Global Proxy
............

Lua objects that bind a :ref:`WpGlobalProxy <global_proxy_api>`
contain the following methods:

.. function:: GlobalProxy.request_destroy(self)

   Binds :c:func:`wp_global_proxy_request_destroy`

   :param self: the proxy

PipeWire Node
.............

Lua objects that bind a :ref:`WpNode <node_api>` contain the following methods:

.. function:: Node.get_state(self)

   Binds :c:func:`wp_node_get_state`

   :param self: the proxy
   :returns: the current state of the node and an error message, if any
   :rtype: string (:c:enum:`WpNodeState`), string (error message)
   :since: 0.4.2

.. function:: Node.get_n_input_ports(self)

   Binds :c:func:`wp_node_get_n_input_ports`

   :param self: the proxy
   :returns: the current and max numbers of input ports on the node
   :rtype: integer (current), integer (max)
   :since: 0.4.2

.. function:: Node.get_n_output_ports(self)

   Binds :c:func:`wp_node_get_n_output_ports`

   :param self: the proxy
   :returns: the current and max numbers of output ports on the node
   :rtype: integer (current), integer (max)
   :since: 0.4.2

.. function:: Node.get_n_ports(self)

   Binds :c:func:`wp_node_get_n_ports`

   :param self: the proxy
   :returns: the number of ports on the node
   :since: 0.4.2

.. function:: Node.iterate_ports(self, interest)

   Binds :c:func:`wp_node_iterate_ports`

   :param self: the proxy
   :param interest: an interest to filter objects
   :type interest: :ref:`Interest <lua_object_interest_api>` or nil or none
   :returns: all the ports of this node that that match the interest
   :rtype: Iterator; the iteration items are of type :ref:`WpPort <port_api>`
   :since: 0.4.2

.. function:: Node.lookup_port(self, interest)

   Binds :c:func:`wp_node_lookup_port`

   :param self: the proxy
   :param interest: the interest to use for the lookup
   :type interest: :ref:`Interest <lua_object_interest_api>` or nil or none
   :returns: the first port of this node that matches the interest
   :rtype: :ref:`WpPort <port_api>`
   :since: 0.4.2

.. function:: Node.send_command(self, command)

   Binds :c:func:`wp_node_send_command`

   :param self: the proxy
   :param string command: the command to send to the node (ex "Suspend")

PipeWire Port
.............

Lua objects that bind a :ref:`WpPort <port_api>` contain the following methods:

.. function:: Port.get_direction(self)

   Binds :c:func:`wp_port_get_direction`

   :param self: the port
   :returns: the direction of the Port
   :rtype: string (:c:enum:`WpDirection`)
   :since: 0.4.2

PipeWire Client
...............

Lua objects that bind a :ref:`WpClient <client_api>`
contain the following methods:

.. function:: Client.update_permissions(self, perms)

   Binds :c:func:`wp_client_update_permissions`

   Takes a table where the keys are object identifiers and the values are
   permission strings.

   Valid object identifiers are:

   - A number, meaning the bound ID of a proxy
   - The string "any" or the string "all", which sets the default permissions
     for this client

   The permission strings have a chmod-like syntax (ex. "rwx" or "r-xm"), where:

   - "r" means permission to read the object
   - "w" means permission to write data to the object
   - "x" means permission to call methods on the object
   - "m" means permission to set metadata for the object
   - "-" is ignored and can be used to make the string more readable when
     a permission flag is omitted

   **Example:**

   .. code-block:: lua

      client:update_permissions {
        ["all"] = "r-x",
        [35] = "rwxm",
      }

   :param self: the proxy
   :param table perms: the permissions to update for this client

.. function:: Client.attach_permission_manager(self, pm)

   Binds :c:func:`wp_client_attach_permission_manager`

   Attaches a permission manager to handle permissions for this client
   automatically. The permission manager will manage per-object permissions
   based on its configured rules and default permissions.

   :param self: the client
   :param WpPermissionManager pm: the permission manager to attach

PipeWire Metadata
.................

Lua objects that bind a :ref:`WpMetadata <metadata_api>`
contain the following methods:

.. function:: Metadata.iterate(self, subject)

   Binds :c:func:`wp_metadata_new_iterator`

   :param self: the proxy
   :param integer subject: the subject id
   :returns: an iterator

.. function:: Metadata.find(self, subject, key)

   Binds :c:func:`wp_metadata_find`

   :param self: the proxy
   :param string subject: the subject id
   :param string key: the metadata key to find
   :returns: the value for this metadata key, the type of the value
   :rtype: string, string

Permission Manager
..................

The ``PermissionManager`` object manages per-object permissions for clients.
It is created with the global ``PermissionManager()`` constructor and configured
with default permissions, core permissions, and match rules.

.. function:: PermissionManager()

   Creates a new permission manager.

   :returns: a new permission manager
   :rtype: WpPermissionManager

.. function:: PermissionManager.set_default_permissions(self, perms)

   Binds :c:func:`wp_permission_manager_set_default_permissions`

   Sets the default permissions applied to all objects that don't match any rule.

   :param self: the permission manager
   :param perms: a permission string (e.g. "rx") or an integer bitmask (e.g. ``Perm.RX``)

.. function:: PermissionManager.set_core_permissions(self, perms)

   Binds :c:func:`wp_permission_manager_set_core_permissions`

   Sets the permissions applied specifically to the PipeWire core object (ID 0).
   If not set, the core inherits the default permissions.

   :param self: the permission manager
   :param perms: a permission string or an integer bitmask

.. function:: PermissionManager.add_rules_match(self, rules)

   Binds :c:func:`wp_permission_manager_add_rules_match`

   Adds a set of match rules that grant specific permissions to objects
   matching the given constraints.

   :param self: the permission manager
   :param WpSpaJson rules: a JSON array of match rules
   :returns: the match id (can be used with ``remove_match``)
   :rtype: integer

.. function:: PermissionManager.add_interest_match(self, callback, interest)

   Binds :c:func:`wp_permission_manager_add_interest_match_closure`

   Adds a dynamic match that calls the given callback to determine permissions
   for objects matching the given interest.

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

   :param self: the permission manager
