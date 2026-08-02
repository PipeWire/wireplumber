.. _lua_spa_device_api:

Spa Device
==========

There are two kinds of device objects in the Lua API.

A ``Device`` is a proxy to a `struct pw_device` that lives in the PipeWire
daemon; it binds :ref:`WpDevice <device_api>` and behaves like any other proxy
(see :ref:`lua_proxies_api`).

A ``SpaDevice`` binds :ref:`WpSpaDevice <spa_device_api>` and runs a SPA device
implementation *inside* the WirePlumber process. This is what all the shipped
device monitors are built on: the SPA plugin discovers hardware and asks, via
the ``create-object`` signal, for objects to be created; the script decides
what to actually create and hands the result back with
:func:`SpaDevice.store_managed_object`. A ``SpaDevice`` is therefore both a
device monitor (``api.alsa.enum.udev``, ``api.bluez5.enum.dbus``,
``api.v4l2.enum.udev``, ...) and, one level down, each individual device that
such a monitor asks to be created.

Constructors
~~~~~~~~~~~~

.. function:: Device(factory, properties)

   Binds :c:func:`wp_device_new_from_factory`

   Creates a device on the PipeWire server by asking the remote factory
   *factory* to create it. The device only exists on the server once
   ``Feature.Proxy.BOUND`` has been activated; see :ref:`lua_object_api`.

   :param string factory: the name of the PipeWire factory
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object with the device properties
   :returns: the new device, or nil if the core is not connected
   :rtype: Device

.. function:: SpaDevice(factory, properties)

   Binds :c:func:`wp_spa_device_new_from_spa_factory`

   Loads the SPA factory *factory* from the SPA plugins and wraps the
   `spa_device` object that it creates.

   The returned object does nothing until it is activated with
   ``Feature.SpaDevice.ENABLED``; see :ref:`lua_object_api`. Connect to the
   signals below *before* activating, otherwise the objects that the device
   creates on startup are missed. For real devices, as opposed to device
   monitors, it is also desirable to export the device to PipeWire by
   activating ``Feature.Proxy.BOUND`` at the same time.

   **Example:**

   .. code-block:: lua

      local device = SpaDevice("api.alsa.enum.udev", properties)
      if device then
        device:connect("create-object", createDevice)
        device:connect("object-removed", removeDevice)
        device:activate(Feature.SpaDevice.ENABLED)
      end

   :param string factory: the name of the SPA factory
   :param properties: *(optional)* a table or
      :ref:`Properties <lua_properties_api>` object with the device properties
   :returns: the new spa device, or nil if the factory could not be loaded
   :rtype: SpaDevice

Signals
~~~~~~~

A ``SpaDevice`` emits the signals of :c:struct:`WpSpaDevice`, which are
connected with :func:`GObject.connect`:

``create-object`` (self, id, type, factory, properties)
   The device is asking for a managed object to be created. The handler is
   expected to construct the object using the requested *factory* and
   *properties* and to store it with :func:`SpaDevice.store_managed_object`
   under the same *id*. Objects that are created asynchronously, which is
   normally the case, should be marked with
   :func:`SpaDevice.set_managed_pending` first and stored once they are ready.

``object-removed`` (self, id)
   The device has deleted the managed object *id*. The handler may release any
   additional resources associated with it. There is no need to remove the
   object with :func:`SpaDevice.store_managed_object`; that happens internally
   right after this signal.

``event`` (self, pod)
   The device emitted an event, as a Spa Pod object.

Methods
~~~~~~~

.. function:: SpaDevice.iterate_params(self, param_name, filter)

   Binds :c:func:`wp_spa_device_enum_params_sync`

   :param self: the spa device
   :param string param_name: the SPA param name to enumerate, ex "EnumProfile"
   :param Pod filter: *(optional)* a Spa Pod object to filter the results
   :returns: the available parameters
   :rtype: :ref:`Iterator <lua_iterator_api>`; the iteration items are Spa Pod objects

.. function:: SpaDevice.set_param(self, param_name, pod)

   Binds :c:func:`wp_spa_device_set_param`

   :param self: the spa device
   :param string param_name: the SPA param name to set, ex "Profile"
   :param Pod pod: a Spa Pod object containing the new params

.. function:: SpaDevice.iterate_managed_objects(self)

   Binds :c:func:`wp_spa_device_new_managed_object_iterator`

   :param self: the spa device
   :returns: all the objects that are currently stored on this device
   :rtype: :ref:`Iterator <lua_iterator_api>`

.. function:: SpaDevice.get_managed_object(self, id)

   Binds :c:func:`wp_spa_device_get_managed_object`

   :param self: the spa device
   :param integer id: the object id
   :returns: the managed object or nil

.. function:: SpaDevice.store_managed_object(self, id, object)

   Binds :c:func:`wp_spa_device_store_managed_object`

   Stores an object under *id*, taking ownership of it. Storing nil destroys
   the object that was previously stored under this id.

   :param self: the spa device
   :param integer id: the object id
   :param GObject object: a GObject to store or nil to remove the existing
                          stored object

.. function:: SpaDevice.set_managed_pending(self, id)

   Binds :c:func:`wp_spa_device_set_managed_pending`

   Marks *id* as pending, meaning that an object for it is being created but
   is not ready yet. Params that the device sets on the object in the meantime
   are saved and applied as soon as :func:`SpaDevice.store_managed_object`
   provides the object. Without this, the settings that a device applies to a
   node right after asking for it to be created are lost.

   This has no effect if an object is already stored under *id*.

   :param self: the spa device
   :param integer id: the object id
