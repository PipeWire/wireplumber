.. _lua_object_api:

WpObject
========

``WpObject`` is the base class of most WirePlumber objects — nodes, devices,
links, metadata, session items and so on. It binds the
:ref:`WpObject <object_api>` C API.

Its distinguishing feature is the *features* mechanism. A newly created object
is not immediately usable in full: proxies to PipeWire objects, for example,
start out knowing only their id, and additional information has to be requested
and awaited. Features name the pieces of functionality that can be activated,
and activation is asynchronous.

Scripts do not normally construct ``WpObject`` directly; they receive objects
from an :ref:`ObjectManager <lua_object_manager_api>` (already activated, as
requested by the object manager) or create a specific subclass such as
:func:`Node` or :func:`SpaDevice`.

Activation
----------

.. function:: Object.activate(self, features, callback)

   Binds :c:func:`wp_object_activate`

   Requests activation of the given features. This is asynchronous: the object
   is only usable for those features once *callback* has been called
   successfully.

   .. code-block:: lua

      device:activate (Feature.SpaDevice.ENABLED, function (dev, err)
        if err then
          log:warning (dev, "failed to activate: " .. err)
          return
        end
        -- the device is now enabled
      end)

   :param integer features: a bitmask of the features to activate; see
      :ref:`the constants below <lua_object_api_features>`
   :param function callback: *(optional)* called when activation completes,
      as ``callback(object, error)``; *error* is nil on success and an error
      message string on failure

.. function:: Object.deactivate(self, features)

   Binds :c:func:`wp_object_deactivate`

   Deactivates the given features, releasing whatever resources they hold.

   :param integer features: a bitmask of the features to deactivate

.. function:: Object.get_active_features(self)

   Binds :c:func:`wp_object_get_active_features`

   :returns: a bitmask of the features that are currently active
   :rtype: integer

.. function:: Object.get_supported_features(self)

   Binds :c:func:`wp_object_get_supported_features`

   :returns: a bitmask of the features that this object is able to activate
   :rtype: integer

.. _lua_object_api_features:

Feature constants
-----------------

Individual features are named by the ``Feature`` table, grouped by the object
type that supports them:

============================================ =====================================
Constant                                     Applies to
============================================ =====================================
``Feature.Proxy.BOUND``                      any proxy
``Feature.PipewireObject.INFO``              any PipeWire object
``Feature.PipewireObject.PARAM_PROPS``       objects with a Props param
``Feature.PipewireObject.PARAM_FORMAT``      objects with a Format param
``Feature.PipewireObject.PARAM_PROFILE``     devices
``Feature.PipewireObject.PARAM_PORT_CONFIG`` nodes
``Feature.PipewireObject.PARAM_ROUTE``       devices
``Feature.SpaDevice.ENABLED``                :ref:`SpaDevice <lua_spa_device_api>`
``Feature.Node.PORTS``                       :ref:`Node <lua_proxies_api>`
``Feature.Metadata.DATA``                    :ref:`Metadata <lua_proxies_api>`
``Feature.SessionItem.ACTIVE``               :ref:`SessionItem <lua_session_item_api>`
``Feature.SessionItem.EXPORTED``             :ref:`SessionItem <lua_session_item_api>`
============================================ =====================================

The ``Features`` table provides convenient combinations:

==================================== ==========================================
Constant                             Meaning
==================================== ==========================================
``Features.ALL``                     all features the object supports
``Features.PipewireObject.MINIMAL``  ``Proxy.BOUND`` plus
                                     ``PipewireObject.INFO``
==================================== ==========================================

Since features are a bitmask, they are combined with the bitwise or operator:

.. code-block:: lua

   node:activate (Features.PipewireObject.MINIMAL | Feature.Node.PORTS,
       function (n, err) --[[ ... ]] end)
