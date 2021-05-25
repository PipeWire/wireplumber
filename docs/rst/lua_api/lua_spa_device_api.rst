 .. _lua_spa_device_api:

Spa Device
==========

.. function:: SpaDevice.get_managed_object(self, id)

   Binds :c:func:`wp_spa_device_get_managed_object`

   :param self: the spa device
   :param integer id: the object id
   :returns: the managed object or nil

.. function:: SpaDevice.store_managed_object(self, id, object)

   Binds :c:func:`wp_spa_device_store_managed_object`

   :param self: the spa device
   :param integer id: the object id
   :param GObject object: a GObject to store or nil to remove the existing
                          stored object
