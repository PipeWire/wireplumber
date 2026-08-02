.. _lua_state_api:

State
=====

Persistent state is how WirePlumber remembers things across restarts: which
profile a device had, which node was the default, what volume a stream was
last set to.

``State`` binds the :ref:`WpState <state_api>` C API and stores a set of
key/value pairs in a file under the user's state directory (see
:ref:`daemon_file_locations`). ``StateMetadata`` additionally mirrors the
stored values into a PipeWire metadata object, so that they can be inspected
and cleared with ``pw-metadata``.

State
-----

.. function:: State(name)

   Binds :c:func:`wp_state_new`

   Creates a state object backed by the state file called *name*.

   :param string name: the name of the state
   :returns: the new state object
   :rtype: State

.. function:: State.load(self)

   Binds :c:func:`wp_state_load`

   Loads the contents of the state file.

   :returns: the stored key/value pairs
   :rtype: Properties, see :ref:`lua_properties_api`

.. function:: State.save(self, properties)

   Binds :c:func:`wp_state_save`

   Writes *properties* to the state file, replacing its previous contents.

   :param properties: a table or :ref:`Properties <lua_properties_api>` object
   :returns: true on success, plus an error message string if it failed
   :rtype: boolean, string

.. function:: State.save_after_timeout(self, properties)

   Binds :c:func:`wp_state_save_after_timeout`

   The same as :func:`State.save`, but delayed by a short timeout, and
   coalescing repeated calls into a single write. This is what the shipped
   scripts use, so that a burst of changes does not cause a burst of disk
   writes.

   :param properties: a table or :ref:`Properties <lua_properties_api>` object

.. function:: State.clear(self)

   Binds :c:func:`wp_state_clear`

   Deletes the state file.

StateMetadata
-------------

.. function:: StateMetadata(name)

   Binds :c:func:`wp_state_metadata_new`

   Creates a state object that also exposes its contents as a PipeWire
   metadata object named *name*.

   :param string name: the name of the state and of the metadata object
   :returns: the new state metadata object
   :rtype: StateMetadata

.. function:: StateMetadata.get(self, key)

   Binds :c:func:`wp_state_metadata_get`

   :param string key: the key to look up
   :returns: the stored value, or nil
   :rtype: string

.. function:: StateMetadata.set(self, key, value)

   Binds :c:func:`wp_state_metadata_set`

   Stores a value. Passing no value removes the key.

   :param string key: the key to store under
   :param string value: *(optional)* the value to store

.. function:: StateMetadata.clear(self)

   Binds :c:func:`wp_state_metadata_clear`

   Removes all the stored values.

The metadata object that ``StateMetadata`` exports is created in the same way
as one created directly with ``ImplMetadata``; see :ref:`lua_proxies_api`.
