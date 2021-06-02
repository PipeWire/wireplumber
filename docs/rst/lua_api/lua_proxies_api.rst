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
   :rtype: Iterator; the iteration items are `Spa Pod <lua_spa_pod>`_ objects

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

.. function:: Node.send_command(self, command)

   Binds :c:func:`wp_node_send_command`

   :param self: the proxy
   :param string command: the command to send to the node (ex "Suspend")


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
