.. _lua_proxies_api:

PipeWire Proxies
================

Constructors
............

Most proxies are obtained from an :ref:`ObjectManager <lua_object_manager_api>`
rather than constructed. The following globals create new objects, each by
asking a PipeWire factory to make one.

.. function:: Node(factory, properties)

   Binds :c:func:`wp_node_new_from_factory`

   Creates a node using the given PipeWire factory. The node lives in the
   PipeWire daemon.

   :param string factory: the name of the PipeWire factory
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object with the node properties
   :returns: the new node, or nil if the factory could not create it
   :rtype: Node

.. function:: LocalNode(factory, properties)

   Binds :c:func:`wp_impl_node_new_from_pw_factory`

   Creates a node that runs inside the WirePlumber process and is exported to
   the PipeWire graph, rather than living in the PipeWire daemon. This is what
   the MIDI bridge and the Bluetooth nodes use.

   The returned object must be activated before it appears in the graph; see
   :ref:`lua_object_api`.

   :param string factory: the name of the PipeWire factory
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object with the node properties
   :returns: the new node, or nil if the factory could not create it
   :rtype: LocalNode

.. function:: Link(factory, properties)

   Binds :c:func:`wp_link_new_from_factory`

   Creates a link. The endpoints are given as properties:
   ``link.output.node``, ``link.output.port``, ``link.input.node`` and
   ``link.input.port``.

   Note that the linking policy does not use this directly; it creates
   ``si-standard-link`` session items instead, which manage the underlying
   links themselves.

   :param string factory: the name of the PipeWire factory, normally
      ``"link-factory"``
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object describing the link
   :returns: the new link, or nil if the factory could not create it
   :rtype: Link

.. function:: ImplMetadata(name, properties)

   Binds :c:func:`wp_impl_metadata_new_full`

   Creates a metadata object that WirePlumber itself exports to the PipeWire
   graph, as opposed to a proxy to one exported by somebody else. This is how
   the ``default``, ``filters`` and ``sm-settings`` metadata objects come into
   existence.

   The returned object must be activated before it appears in the graph; see
   :ref:`lua_object_api`. It supports the same methods as a metadata proxy;
   see `PipeWire Metadata`_ below.

   :param string name: the name of the metadata object
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object with additional properties
   :returns: the new metadata object
   :rtype: ImplMetadata

See also ``Device`` and ``SpaDevice`` in :ref:`lua_spa_device_api`.

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
   :rtype: :ref:`Iterator <lua_iterator_api>`; the iteration items are Spa Pod objects

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

   Binds :c:func:`wp_node_new_ports_iterator`

   :param self: the proxy
   :param interest: an interest to filter objects
   :type interest: :ref:`Interest <lua_object_interest_api>` or nil or none
   :returns: all the ports of this node that that match the interest
   :rtype: :ref:`Iterator <lua_iterator_api>`; the iteration items are of type :ref:`WpPort <port_api>`
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
contain the following methods. See :ref:`lua_permissions_api` for the ``Perm``
constants and the ``PermissionManager`` object that these methods work with.

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
   - "l" means permission to link to the object without being able to see it
   - "-" is ignored and can be used to make the string more readable when
     a permission flag is omitted

   The string "all" is also accepted and is a synonym for "rwxm".

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
   based on its configured rules and default permissions; see
   :ref:`lua_permissions_api`. It must have been activated with
   ``Features.ALL`` before it is attached.

   :param self: the client
   :param WpPermissionManager pm: the permission manager to attach

.. function:: Client.get_permission_manager(self)

   Binds :c:func:`wp_client_get_permission_manager`

   Returns the permission manager currently attached to this client, or ``nil``
   if no permission manager is attached.

   **Example:**

   .. code-block:: lua

      local pm = client:get_permission_manager()
      if pm then
        local perms = pm:get_default_permissions()
        -- check permission bits
      end

   :param self: the client
   :returns: the attached permission manager, or nil
   :rtype: WpPermissionManager or nil

PipeWire Metadata
.................

Lua objects that bind a :ref:`WpMetadata <metadata_api>`
contain the following methods:

.. function:: Metadata.iterate(self, subject)

   Binds :c:func:`wp_metadata_new_iterator`

   :param self: the proxy
   :param integer subject: the subject id
   :returns: an iteration over the metadata entries of this subject, to be used
             in a ``for`` loop; each step yields the subject id, the key, the
             value type and the value

   **Example:**

   .. code-block:: lua

      for subject, key, type, value in metadata:iterate (-1) do
        log:info (tostring (subject) .. " " .. key .. " = " .. value)
      end

.. function:: Metadata.find(self, subject, key)

   Binds :c:func:`wp_metadata_find`

   :param self: the proxy
   :param string subject: the subject id
   :param string key: the metadata key to find
   :returns: the value for this metadata key, the type of the value
   :rtype: string, string

.. function:: Metadata.set(self, subject, key, type, value)

   Binds :c:func:`wp_metadata_set`

   Stores a value. Passing no *value* removes the key; passing no *key* either
   removes all the metadata associated with *subject*.

   Note that this only takes effect if the client has the "m" permission on
   *subject*; see :func:`Client.update_permissions`. On a metadata proxy, the
   change is also not visible to :func:`Metadata.find` until the next
   round-trip with the PipeWire server.

   :param self: the proxy
   :param integer subject: the subject id
   :param string key: *(optional)* the key to set
   :param string type: *(optional)* the type of the value; nil means "string"
   :param string value: *(optional)* the value to set

