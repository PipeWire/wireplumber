.. _lua_events_api:

Events & Hooks
==============

This is the API that scripts use to participate in WirePlumber's event-driven
session management logic. Almost all of the policy that ships with WirePlumber
is implemented as hooks registered through this API.

For a conceptual introduction to events and hooks, read
:ref:`design_events_and_hooks` first. The underlying C API is documented in
:ref:`event_api`, :ref:`event_dispatcher_api` and :ref:`event_hook_api`.

Overview
--------

An **event** is something that happened, pushed onto the event stack. Events
carry a *type* (for example ``node-added``), a *priority*, a set of
*properties*, a *source* and a *subject*.

A **hook** is a piece of code that runs when a matching event is dispatched.
A hook declares:

- a unique ``name``, which other hooks use to order themselves against it
- one or more ``interests``, which decide whether the hook runs for a given
  event
- ``before`` / ``after`` constraints, which order it relative to other hooks
  running on the same event
- the code to run, either as a single ``execute`` function
  (:func:`SimpleEventHook`) or as a state machine (:func:`AsyncEventHook`)

Hooks do not run until they are registered with :func:`EventHook.register`.
A minimal hook therefore looks like this:

.. code-block:: lua

   SimpleEventHook {
     name = "example/log-new-nodes",
     interests = {
       EventInterest {
         Constraint { "event.type", "=", "node-added" },
       },
     },
     execute = function (event)
       local node = event:get_subject ()
       log:info (node, "node added: " .. tostring (node.properties ["node.name"]))
     end
   }:register ()

Event
-----

The event object is passed to the hook's ``execute`` function.

.. function:: Event.get_properties(self)

   Binds :c:func:`wp_event_get_properties`

   Returns the properties of the event. These always include ``event.type``,
   and typically include properties copied from the subject, which is what
   makes it possible to match on them in an :func:`EventInterest`.

   :returns: the properties of the event
   :rtype: Properties, see :ref:`lua_properties_api`

.. function:: Event.get_source(self)

   Binds :c:func:`wp_event_get_source`

   Returns the object that pushed this event. For events pushed by the
   standard event source, this is the *standard-event-source* plugin, which
   is also the object that provides the ``schedule-rescan`` and
   ``get-object-manager`` actions used by the shipped scripts.

   :returns: the source of the event
   :rtype: GObject

.. function:: Event.get_subject(self)

   Binds :c:func:`wp_event_get_subject`

   Returns the object that this event is about; for a ``node-added`` event
   this is the :class:`Node` that was added.

   :returns: the subject of the event
   :rtype: GObject

.. function:: Event.stop_processing(self)

   Binds :c:func:`wp_event_stop_processing`

   Stops the event from being processed any further. No hooks that would run
   after this one will run for this event.

.. function:: Event.set_data(self, key, value)

   Binds :c:func:`wp_event_set_data`

   Stores an arbitrary value on the event, so that later hooks running on the
   same event can read it with :func:`Event.get_data`. This is the standard
   way for a chain of hooks to communicate; the ``select-target`` chain, for
   instance, uses it to pass the selected target from the ``find-*`` hooks to
   ``linking/link-target``.

   Passing no value (or nil) removes the data.

   :param string key: the key to store the value under
   :param value: the value to store; booleans, numbers, strings, tables
      (stored as :class:`Properties`) and GValue userdata are supported

.. function:: Event.get_data(self, key)

   Binds :c:func:`wp_event_get_data`

   Returns the value previously stored with :func:`Event.set_data`, or nil.

   :param string key: the key to look up
   :returns: the stored value, or nil

EventInterest
-------------

.. function:: EventInterest(decl)

   Creates an :ref:`ObjectInterest <lua_object_interest_api>` that matches
   :class:`Event` objects. It is a shorthand for
   ``Interest { type = "event", ... }``.

   The constraints are matched against the event's *properties*, so
   ``event.type`` is almost always the first constraint. See
   :ref:`lua_object_interest_api` for the full constraint syntax.

   .. code-block:: lua

      EventInterest {
        Constraint { "event.type", "=", "node-added" },
        Constraint { "media.class", "#", "Stream/*" },
      }

   When an event is created, the properties of its subject - both the info
   properties and the global properties - are merged into the event's own
   properties, which is why the ``media.class`` constraint above works without
   any further qualification. Consequently, the constraint ``type`` is
   irrelevant for ``"pw"`` and ``"pw-global"`` constraints on an event
   interest; both look up the same merged set of properties. A ``"gobject"``
   constraint, on the other hand, is still matched against the GObject
   properties of the event's subject.

   A hook may declare several interests; the hook runs if *any* of them
   matches.

   :returns: the new event interest
   :rtype: ObjectInterest

EventHook
---------

Both :func:`SimpleEventHook` and :func:`AsyncEventHook` return an object with
the following methods.

.. function:: EventHook.register(self)

   Binds :c:func:`wp_event_dispatcher_register_hook`

   Registers the hook with the event dispatcher, so that it starts running for
   matching events. A hook has no effect until this is called.

.. function:: EventHook.remove(self)

   Binds :c:func:`wp_event_dispatcher_unregister_hook`

   Unregisters the hook. This is how scripts enable and disable hooks at
   runtime in response to a setting change; see
   :ref:`the example below <lua_events_api_toggle>`.

SimpleEventHook
---------------

.. function:: SimpleEventHook(decl)

   Binds :c:func:`wp_simple_event_hook_new`

   Constructs a hook whose action is a single synchronous function.

   The constructor takes a single table with the following fields:

   ============ ========================================================
   Field        Contains
   ============ ========================================================
   name         *(required)* a string, the unique name of the hook
   execute      *(required)* the function to run; it takes the
                :class:`Event` as its only argument and returns nothing
   interests    a list of :func:`EventInterest` objects; the hook runs if
                any of them matches
   before       a hook name, or a list of hook names, that must run
                *after* this hook
   after        a hook name, or a list of hook names, that must run
                *before* this hook
   ============ ========================================================

   .. code-block:: lua

      SimpleEventHook {
        name = "linking/find-default-target",
        after = { "linking/find-defined-target",
                  "linking/find-filter-target" },
        before = "linking/prepare-link",
        interests = {
          EventInterest {
            Constraint { "event.type", "=", "select-target" },
          },
        },
        execute = function (event)
          -- ...
        end
      }:register ()

   :returns: the new hook
   :rtype: EventHook

AsyncEventHook
--------------

.. function:: AsyncEventHook(decl)

   Binds :c:func:`wp_async_event_hook_new`

   Constructs a hook whose action is a state machine, for actions that cannot
   complete synchronously — typically because they need to wait for an object
   to activate or for a PipeWire round trip.

   The constructor takes the same ``name``, ``interests``, ``before`` and
   ``after`` fields as :func:`SimpleEventHook`, but instead of ``execute`` it
   takes a ``steps`` table.

   Each entry in ``steps`` is named by a string and contains:

   ============ ========================================================
   Field        Contains
   ============ ========================================================
   next         *(required)* the name of the step to run next, or the
                string ``"none"`` to finish
   execute      *(required)* the function to run for this step; it takes
                the :class:`Event` and a :ref:`Transition <transitions_api>`
   ============ ========================================================

   Execution always starts at the step named ``start`` and follows the
   ``next`` links until a step whose ``next`` is ``"none"`` has run.

   Each step's ``execute`` function is responsible for advancing the
   transition, either immediately with ``transition:advance ()`` or later from
   an asynchronous callback. Calling ``transition:return_error (message)``
   aborts the hook.

   A step named ``error`` is optional; if present, it runs when the transition
   fails.

   .. code-block:: lua

      AsyncEventHook {
        name = "node/create-item",
        interests = {
          EventInterest {
            Constraint { "event.type", "=", "node-added" },
          },
        },
        steps = {
          start = {
            next = "register",
            execute = function (event, transition)
              local node = event:get_subject ()
              -- ... create and configure the item ...
              transition:advance ()
            end
          },
          register = {
            next = "none",
            execute = function (event, transition)
              -- ... register the item ...
              transition:advance ()
            end
          },
        },
      }:register ()

   :returns: the new hook
   :rtype: EventHook

EventDispatcher
---------------

.. function:: EventDispatcher.push_event(event)

   Binds :c:func:`wp_event_dispatcher_push_event`

   Pushes an event onto the event stack, where it will be picked up and
   dispatched to all the hooks that are interested in it.

   The argument is either an existing :class:`Event`, or a table describing a
   new event to construct and push:

   ============ ========================================================
   Field        Contains
   ============ ========================================================
   type         *(required)* a string, the event type
   priority     *(required)* a number; higher priority events are
                dispatched before lower priority ones
   properties   a table or :class:`Properties` with additional properties
                for the event
   source       the object pushing the event
   subject      the object the event is about
   ============ ========================================================

   Properties of the subject are automatically copied into the event's
   properties, so interests can match on them.

   In practice, the shipped scripts rarely build the table themselves. They
   ask the *standard-event-source* plugin to create the event instead, which
   assigns the priority that is configured for that event type, then attach
   any extra data to it and push it:

   .. code-block:: lua

      source = source or Plugin.find ("standard-event-source")

      local e = source:call ("create-event", "create-v4l2-device", parent, nil)
      e:set_data ("device-properties", properties)
      e:set_data ("factory", factory)

      EventDispatcher.push_event (e)

   The ``create-event`` action takes the event type, the subject and an
   optional properties table.

   :returns: the event that was pushed
   :rtype: Event

.. _lua_events_api_toggle:

Enabling and disabling hooks at runtime
---------------------------------------

Because :func:`EventHook.register` and :func:`EventHook.remove` can be called
at any time, a script can react to a setting changing by adding or removing a
hook, rather than by checking the setting on every event. This is the pattern
used by, for example, ``linking/rescan-trigger-on-target-metadata-changed``:

.. code-block:: lua

   local hook = nil

   local function updateEnabled (enable)
     if enable and not hook then
       hook = SimpleEventHook {
         name = "example/my-hook",
         interests = { EventInterest { Constraint { "event.type", "=", "node-added" } } },
         execute = function (event) --[[ ... ]] end
       }
       hook:register ()
     elseif hook and not enable then
       hook:remove ()
       hook = nil
     end
   end

   Settings.subscribe ("example.my-setting", function ()
     updateEnabled (Settings.get_boolean ("example.my-setting"))
   end)
   updateEnabled (Settings.get_boolean ("example.my-setting"))

See also :ref:`lua_settings_api` and :ref:`scripting_custom_scripts`.
