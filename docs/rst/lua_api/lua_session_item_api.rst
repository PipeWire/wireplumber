 .. _lua_session_item_api:

Session Item
============

Lua objects that bind a :ref:`WpSessionItem <session_item_api>`
contain the following methods:

.. function:: SessionItem.get_associated_proxy(self, type)

   Binds :c:func:`wp_session_item_get_associated_proxy`

   :param self: the session item
   :param string type: the proxy type name
   :returns: the proxy object or nil

.. function:: SessionItem.reset(self)

   Binds :c:func:`wp_session_item_reset`

   :param self: the session item

.. function:: SessionItem.configure(self, properties)

   Binds :c:func:`wp_session_item_configure`

   :param self: the session item
   :param table properties: The configuration properties
   :returns: true on success, false on failure
   :rtype: boolean

.. function:: SessionItem.register(self)

   Binds :c:func:`wp_session_item_register`

   :param self: the session item

.. function:: SessionItem.remove(self)

   Binds :c:func:`wp_session_item_remove`

   :param self: the session item
