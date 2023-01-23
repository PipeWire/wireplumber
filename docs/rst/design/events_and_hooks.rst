 .. _events_and_hooks:

Events and Hooks
================

Session management is all about reacting to events and taking neccessary
actions. This is why WirePlumber's logic is all built on events and hooks.

Events
------

Events are objects that represent a change that has just happened on a PipeWire
object, or just a trigger for making a decision and potentially taking some
action.

Every event has a source, a subject and some properties, which include the
event type.

* The ``source`` is a reference to the GObject that created this event.
  Typically, this is the ``WpStandardEventSource`` plugin.
* The ``subject`` is an *optional* reference to the object that this event
  is about. For example, in a ``node-added`` event, the ``subject`` would be
  a reference to the ``WpNode`` object that was just added. Some events,
  especially those which are used only to trigger actions, do not have a
  subject.
* The ``properties`` is a dictionary that contains information about the event,
  including the event type, and also includes all the PipeWire properties of the
  ``subject``, if there is one.
* The ``event.type`` property describes the nature of the event, for example
  ``node-added`` or ``metadata-changed`` are some valid event types.

Every event also has a priority. Events with a higher priority are processed
before events with a lower priority. When two or more events have the same
priority, they are processed in a first-in-first-out manner. This logic
is defined in the *event dispatcher*.

Events are short-lived objects. They are created at the time that something is
happening and they are destroyed after they get processed. Processing an event
means executing all the hooks that are associated with it. The next section
explains what hooks are and how they are associated with events.

Hooks
-----

Hooks are objects that represent a runnable action that needs to be executed
when a certain event is processed. Every hook, therefore, consists of a
function - synchronous or asynchronous - that can be executed. Additionally,
every hook has a means to associate itself with specific events. This is
normally done by declaring *interest* to specific event properties or
combinations of them.

There are two main types of hooks: ``SimpleEventHook`` and ``AsyncEventHook``.

* ``SimpleEventHook`` contains a single, synchronous function. As soon as this
  function is executed, the hook is completed.
* ``AsyncEventHook`` contains multiple functions, combined together in a state
  machine using ``WpTransition`` underneath. The hook is completed only after
  the state machine reaches its final state and this can take any amount of time
  neccessary.

Every hook also has a name, which can be an arbitrary string of characters.
Additionally, it has two arrays of names, which declare dependencies between
this hook and others. One array is called ``before`` and the other is called
``after``. The hook names in the ``before`` array specify that this hook must
be executed *before* those other hooks. Similarly, the hook names in the
``after`` array specify that this hook must be executed *after* those other
hooks. Using this mechanism, it is possible to define the order in which
hooks will be executed, for a specific event.

Hooks are long-lived objects. They are created once, registered in the
*event dispatcher*, they are attached on events and detached after their
execution. They don't maintain any internal state, so the actions of the hook
depend solely on the event itself.

The Event Dispatcher
--------------------

The event dispatcher is a (per core) singleton object that processes all events
and also maintains a list of all the registered hooks. It has a method to
*push* events on it, which causes them to be scheduled for processing.

Scheduling of events and hooks
------------------------------

The main idea and reasoning behind this architecture is to have everything
execute in a predefined order and always wait for an action to finish before
executing the next one.

Every event has a *priority* and every hook also has an order of execution that
derives from the inter-dependencies between hooks, which are defined with
``before`` and ``after`` (see above). When an event is pushed on the dispatcher,
the dispatcher goes through all the registered hooks and checks which hooks are
configured to run on this event (their event interest matches the event).
It then makes a list of them, sorted by their order of execution, and stores it
on the event. The event is then added on the dispatcher's list of events, which
is sorted by priority.

For example::

  List of events
   |  event1 (prio 99) -> hook1, hook2, hook3
   |  event2 (prio 50) -> hook5, hook2, hook4
   v

The dispatcher has an internal ``GSource`` that is registered with
``G_PRIORITY_HIGH_IDLE`` priority. When there is at least one event in the
list of events, the source is dispatched. Every time it gets dispatched,
it takes the top-most event (the highest priority one) and executes the highest
priority hook in that event. If the hook executes synchronously, it then takes
the next hook and continues until there are no more hooks on this event;
then it goes to the next event, and so on. If the hook, however, executes
asynchronously, processing stops until the hook finishes; after finishing,
processing resumes like before.

It is important to notice here that the list of events may be modified while
events are getting processed. For example, a device is added; that's a
``device-added`` event. Then a hook is executed to set the profile. That creates
nodes, so a couple of ``node-added`` events... But there is also another hook to
set the route, which was attached on the ``device-added`` event for the device.
Suppose that we give the ``node-added`` events lower priority than the
``device-added`` events, then the ``set-route`` hook will execute right after
the ``set-profile`` and before any ``node-added`` events are processed.

Visually, with sample priorities::

  List of events
   |  "device-added" (prio 20) -> set-profile, set-route
   |  "node-added" (prio 10) -> restore-stream, create-session-item
   v

Obviously, there can also be a case where a newly added event has higher
priority than the event that was being processed before. In that case,
processing the hooks of the original event is stopped until all the hooks from
the higher priority event have been processed. For example, a capture stream
node being added may trigger the "bluetooth autoswitch" hook, which will then
change the profile of a device. Changing the profile also has to trigger setting
a new route and also handling the new device nodes, creating session items for
them... After all this is done, processing the original capture stream
``node-added`` event can continue.
