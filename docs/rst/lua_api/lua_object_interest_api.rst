 .. _lua_object_interest_api:

Object Interest
===============

Constructors
~~~~~~~~~~~~

.. function:: Interest(decl)

   :param table decl: an interest declaration
   :returns: the interest
   :rtype: ObjectInterest

Methods
~~~~~~~

.. function:: Interest.matches(self, obj)

   :param self: the interest
   :param obj: an object to check for a match
   :type obj: GObject or table
   :returns: whether the object matches the interest
   :rtype: boolean
