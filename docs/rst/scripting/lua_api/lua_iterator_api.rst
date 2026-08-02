.. _lua_iterator_api:

Iterator
========

``Iterator`` binds the :ref:`WpIterator <iterator_api>` C API, which is the
generic mechanism that the library uses to return collections of items.
Scripts get one from :func:`ObjectManager.iterate`, ``Node.iterate_params()``,
:func:`Settings.iterate` and several other APIs.

These functions do not return a plain iterator object; they return the pair of
values that a Lua ``for`` loop expects, so they are meant to be used directly
in a ``for`` loop:

.. code-block:: lua

   for node in om:iterate { type = "node" } do
     do_stuff (node)
   end

The type of the items depends on the API that returned the iterator; it is
documented on each of those functions.

.. warning::

   Because two values are returned, assigning the result to a single variable
   captures the *iteration function*, not the iterator:

   .. code-block:: lua

      -- WRONG: 'it' is a function here, not an Iterator
      local it = node:iterate_params ("Props")

   The iterator object is the second return value:

   .. code-block:: lua

      local _, it = node:iterate_params ("Props")

Methods
-------

The following methods are available on the iterator object, for the cases
where a plain ``for`` loop is not enough:

.. function:: Iterator.next(self)

   Binds :c:func:`wp_iterator_next`

   Advances the iterator and returns the next item.

   :returns: the next item, or nil when the iteration is finished

.. function:: Iterator.reset(self)

   Binds :c:func:`wp_iterator_reset`

   Rewinds the iterator back to the beginning, so that it can be iterated
   again.

.. function:: Iterator.iterate(self)

   Returns the iterator again in the form expected by a Lua ``for`` loop, so
   that an iterator held in a variable can be looped over:

   .. code-block:: lua

      local _, it = node:iterate_params ("Props")
      for pod in it:iterate () do
        -- ...
      end

.. note::

   Some APIs, such as :func:`Settings.iterate` and ``Metadata.iterate()``,
   yield several values per step rather than a single item. These methods do
   not know about that: they always apply the generic item conversion, so
   ``next()`` and ``iterate()`` on such an iterator return the raw item object
   instead of the values that the ``for`` loop would have yielded. Use the
   ``for`` loop for those APIs.
