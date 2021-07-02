 .. _lua_object_manager_api:

Object Manager
==============

The ObjectManager (binding for :c:struct:`WpObjectManager`) provides a way to
collect a set of objects and be notified when objects that fulfill a certain
set of criteria are created or destroyed.

To start an object manager, you first need to declare interest in a certain
kind of object specifying a set of :ref:`Interests <lua_object_interest_api>`
in the constructor and then you need to activate it by calling
:func:`ObjectManager.activate`

Upon activating an ObjectManager, any pre-existing objects that match the
specified interests will immediately become available to get through
:func:`ObjectManager.iterate` and the :c:struct:`WpObjectManager` "object-added"
signal will be emitted for all of them.

Constructors
~~~~~~~~~~~~

.. function:: ObjectManager(interest_list)

   Constructs a new object manager.

   Combines :c:func:`wp_object_manager_new` and
   :c:func:`wp_object_manager_add_interest_full`

   The argument needs to be a table that contains one or more
   :ref:`Interest <lua_object_interest_api>` objects. The object manager
   will then contain all objects that match any one of the supplied interests.

   Example:

   .. code-block:: lua

      streams_om = ObjectManager {
        -- match stream nodes
        Interest {
          type = "node",
          Constraint { "media.class", "matches", "Stream/*", type = "pw-global" },
        },
        -- and device nodes that are not associated with any routes
        Interest {
          type = "node",
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "equals", "0", type = "pw" },
        },
        Interest {
          type = "node",
          Constraint { "media.class", "matches", "Audio/*", type = "pw-global" },
          Constraint { "device.routes", "is-absent", type = "pw" },
        },
      }

   The above example will create an ObjectManager that matches all nodes with
   a "media.class" global property that starts with the string "Stream/"
   and additionally all those whose "media.class" starts with "Audio/" and
   they have either a "device.routes" property that equals zero or they
   don't have a "device.routes" property at all.

   :param table interest_list: a list of :ref:`interests <lua_object_interest_api>`
                               to objects
   :returns: a new object manager
   :rtype: ObjectManager (:c:struct:`WpObjectManager`)

Methods
~~~~~~~

.. function:: ObjectManager.activate(self)

   Activates the object manager.
   Binds :c:func:`wp_core_install_object_manager`.

   :param self: the object manager

.. function:: ObjectManager.get_n_objects(self)

    Binds :c:func:`wp_object_manager_get_n_objects`

   :param self: the object manager
   :returns: the number of objects managed by the object manager
   :rtype: integer

.. function:: ObjectManager.iterate(self, interest)

   Binds :c:func:`wp_object_manager_new_filtered_iterator_full`

   :param self: the object manager
   :param interest: an interest to filter objects
   :type interest: :ref:`Interest <lua_object_interest_api>` or nil or none
   :returns: all the managed objects that match the interest
   :rtype: Iterator; the iteration items are of type :ref:`GObject <lua_gobject>`

.. function:: ObjectManager.lookup(self, interest)

   Binds :c:func:`wp_object_manager_lookup`

   :param self: the object manager
   :param interest: the interest to use for the lookup
   :type interest: :ref:`Interest <lua_object_interest_api>` or nil or none
   :returns: the first managed object that matches the interest
   :rtype: :ref:`GObject <lua_gobject>`
