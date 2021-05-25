 .. _lua_object_manager_api:

Object Manager
==============

Constructors
~~~~~~~~~~~~

.. function:: ObjectManager(interest_list)

   Constructs a new object manager.

   Combines :c:func:`wp_object_manager_new` and
   :c:func:`wp_object_manager_add_interest_full`

   :param table interest_list: a list of `interests <lua_object_interest_api>`_
                               to objects
   :returns: a new object manager
   :rtype: ObjectManager

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
   :param ObjectInterest interest: an interest to filter objects, or nil
   :returns: all the managed objects that match the interest
   :rtype: Iterator; the iteration items are of type `GObject <lua_gobject>`_

.. function:: ObjectManager.lookup(self, interest)

   Binds :c:func:`wp_object_manager_lookup`

   :param self: the object manager
   :param ObjectInterest interest: the interest to use for the lookup, or nil
   :returns: the first managed object that matches the interest
   :rtype: `GObject <lua_gobject>`_
