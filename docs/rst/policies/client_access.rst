.. _policies_client_access:

Client Access Control
=====================

Introduction
------------

PipeWire itself does not decide what a client application is allowed to do.
It only enforces a set of per-object permission flags that someone else has to
supply. Supplying them is one of the
:ref:`session manager's responsibilities <design_understanding_session_management>`:
when a client connects, WirePlumber inspects it, decides how much of the graph
it should be able to see and touch, and pushes that decision back to the
PipeWire daemon.

The client access policy is the part of WirePlumber that makes this decision.
It is built out of two pieces:

* a chain of :ref:`event hooks <design_events_and_hooks>` that *select* an
  access category and a permission source for each new client, and
* the ``WpPermissionManager`` object, which *computes* the actual permission
  set for a client and keeps it up to date as the graph changes.

This page describes how both pieces work. For the configuration file syntax,
see :ref:`config_access`; for the Lua API, see :ref:`lua_permissions_api`.

.. _policies_client_access_model:

The permission model
--------------------

PipeWire permissions are a bitmask, evaluated per (client, object) pair:

.. list-table::
   :widths: 10 15 75
   :header-rows: 1

   * - Flag
     - Value
     - Meaning
   * - ``r``
     - ``0400``
     - **read**: the object is visible to the client, i.e. it appears on the
       registry and its properties can be listed
   * - ``w``
     - ``0200``
     - **write**: the client may call methods that modify the object
   * - ``x``
     - ``0100``
     - **execute**: the client may call methods on the object; the ``w`` flag
       must also be present for methods that modify it
   * - ``m``
     - ``0010``
     - **metadata**: the client may set metadata on the object
   * - ``l``
     - ``0020``
     - **link**: nodes of this client may link to this node even if the client
       cannot "see" it (i.e. it has no ``r`` permission on it)

Permissions are expressed either as chmod-like strings (``"rwx"``, ``"r-xm"``,
where ``-`` is ignored and only aids readability) or, in Lua, as ``Perm``
constants. The special string ``"all"`` and the constant ``Perm.ALL`` both mean
``rwxm``; note that neither includes ``l``.

A client's permissions are stored by PipeWire as a list of
``(object id, permissions)`` entries. Two ids are special:

* ``PW_ID_ANY`` is the fallback entry. It applies to every object for which
  there is no more specific entry, including objects that do not exist yet.
  This is what the Lua ``client:update_permissions { ["any"] = ... }`` key and
  the ``default_permissions`` configuration property set.
* ``PW_ID_CORE`` (id ``0``) is the PipeWire core object. A client that has no
  ``r`` permission on the core cannot complete its connection handshake, so
  this entry is often granted even when everything else is denied.

Because a per-object entry *replaces* the fallback rather than adding to it,
granting an object no permissions at all is how an object is hidden from a
client.

Selecting access for a client
-----------------------------

Every client that appears on the registry produces a ``client-added`` event.
The ``client/select-access-trigger`` hook turns that into a ``select-access``
event whose subject is the client, and the rest of the policy runs on that
event.

.. graphviz::

   digraph client_access {
     rankdir=LR;
     node [shape=box, fontsize=10];

     added [label="client-added"];
     sel [label="select-access"];
     config [label="find-config-access"];
     sandbox [label="find-flatpak-access\nfind-snap-access\nfind-portal-access"];
     def [label="find-default-access"];
     apply [label="apply-access", shape=box, style=bold];

     added -> sel [label="select-access-trigger"];
     sel -> config -> sandbox -> def -> apply;
   }

The hooks on the ``select-access`` event do not talk to each other directly.
Instead, each of them may fill in three pieces of event data:

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Event data
     - Meaning
   * - ``effective-access``
     - A string naming the access category that was recognised for this client
       (``"flatpak"``, ``"flatpak-manager"``, ``"restricted"``, ...). This is
       informational; it is published on the client as the
       ``pipewire.access.effective`` property so that other software can tell
       how the client was classified.
   * - ``default-permissions``
     - A permission string to apply directly as the client's fallback
       permissions. Setting this bypasses permission managers entirely.
   * - ``permission-manager``
     - A ``WpPermissionManager`` to attach to the client, which will then
       compute and maintain its permissions dynamically.

**Each slot is written only if it is still empty.** The hooks are ordered so
that the more specific source wins: whichever hook runs first and recognises
the client determines the outcome, and later hooks leave that slot alone. This
is what makes the policy extensible — a custom hook that runs early can claim a
client, and the shipped hooks will then not override it.

The shipped hooks, in execution order:

.. list-table::
   :widths: 30 25 45
   :header-rows: 1

   * - Hook name
     - File
     - What it does
   * - ``client/select-access-trigger``
     - ``select-access.lua``
     - Pushes the ``select-access`` event for every added client.
   * - ``client/find-config-access``
     - ``find-config-access.lua``
     - Matches the client against ``access.rules`` from the configuration and
       may set any of the three slots. Permission managers declared in
       ``access.permission-managers`` are instantiated once at script load time
       and referenced here by name.
   * - ``client/find-flatpak-access``
     - ``find-flatpak-access.lua``
     - Recognises Flatpak clients. Flatpak "Manager" clients get a permission
       manager with ``Perm.ALL`` defaults, other Flatpak clients get
       ``Perm.RX``.
   * - ``client/find-snap-access``
     - ``find-snap-access.lua``
     - Recognises Snap clients and attaches a permission manager that hides
       other snaps' objects and gates audio sinks and sources on the snap's
       ``playback`` and ``record`` interfaces.
   * - ``client/find-portal-access``
     - ``find-portal-access.lua``
     - Recognises clients coming through the XDG desktop portal and attaches a
       permission manager that gates camera nodes on the portal permission
       store.
   * - ``client/find-default-access``
     - ``find-default-access.lua``
     - The fallback. Attaches a permission manager with ``Perm.ALL`` defaults,
       or ``Perm.RX`` if the client's access is ``"restricted"``.
   * - ``client/apply-access``
     - ``apply-access.lua``
     - Applies the result; see below.

The three sandbox hooks all declare themselves as running after
``client/find-config-access`` and before ``client/find-default-access``. Their
relative order is not specified, which is harmless because each of them only
acts on clients belonging to its own sandboxing technology.

Applying the decision
---------------------

``client/apply-access`` is an asynchronous hook that consumes the event data:

#. If ``effective-access`` was set, it is written to the client's
   ``pipewire.access.effective`` property.
#. If ``default-permissions`` was set, it is applied directly with
   ``client:update_permissions { ["any"] = ... }`` and the hook is done. Any
   permission manager that was also selected is ignored — this is why setting
   ``default_permissions`` in ``access.rules`` overrides
   ``permission_manager_name``.
#. Otherwise, if a ``permission-manager`` was set, it is activated (if it is not
   active already) and then attached to the client. From that point on the
   permission manager owns the client's permissions.
#. If neither was set, the client is left with whatever permissions PipeWire
   gave it.

.. _policies_client_access_pm:

Permission managers
-------------------

A ``WpPermissionManager`` is a reusable, stateful object that computes a
permission list for each of the clients attached to it. A single instance is
normally shared by all clients of the same category — the shipped scripts
create one per category at load time and attach it to every matching client —
so its cost does not grow with the number of clients.

A permission manager is configured with:

* **Default permissions** — the ``PW_ID_ANY`` fallback entry. A newly created
  permission manager starts with ``rwx``.
* **Core permissions** — an optional entry for ``PW_ID_CORE``. If it is not set
  explicitly, no separate entry is emitted and the core falls under the
  default. Setting it is useful when the default is restrictive: the client can
  still connect and enumerate, while individual objects remain hidden.
* **Matches** — zero or more rules that grant permissions to specific objects.

Three kinds of match are available:

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Kind
     - Description
   * - Rules match
     - A SPA-JSON array in the same ``matches``/``actions`` form used
       everywhere else in the configuration, with a ``set-permissions`` action
       whose value is a permission string. The constraints are evaluated
       against both the object's global properties and, for PipeWire objects,
       its full property list.
   * - Interest match with a callback
     - A :ref:`WpObjectInterest <lua_object_interest_api>` plus a function that
       is called with the permission manager, the client and the matched
       object, and returns the permissions to grant. This is how a decision can
       depend on the client, not just on the object.
   * - Static interest match
     - A ``WpObjectInterest`` plus a fixed permission bitmask.

Every match is added to the manager independently and returns an id that can be
used to remove it later.

Computing the permission list
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When a permission manager is activated it installs an object manager over all
global objects. To build the permission list for a client it then:

#. emits the ``PW_ID_ANY`` entry with the default permissions, and the
   ``PW_ID_CORE`` entry if core permissions were set explicitly;
#. walks every global object and evaluates every match against it, emitting an
   entry for each match that applies;
#. merges entries that refer to the same object id by OR-ing their permission
   bits together.

Because the merge is a bitwise OR, the order in which matches were added does
not affect the result. An object that no match applies to gets no entry of its
own and therefore falls under the default permissions.

The list is then pushed to every attached client.

When permissions are recomputed
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Permissions are recomputed when:

* a client is attached to the permission manager;
* a global object is added or removed *and* at least one match applies to it —
  objects that no match cares about do not trigger a recomputation;
* something calls ``update_permissions()`` on the manager explicitly.

There is deliberately no automatic recomputation on property changes, since
that would mean re-evaluating every match on every property update in the
graph. Policies whose decisions depend on external state are expected to
trigger the update themselves. The portal policy, for example, watches the
portal permission store for changes and calls ``update_permissions()`` when the
camera permissions change.

For decisions that depend on the *client's* properties changing, a permission
manager emits a ``client-properties-changed`` signal for every attached client.
The portal policy uses this to detect the moment the PipeWire daemon gates a
portal client and to ungate it once permissions are in place.

Enabling and extending
----------------------

The policy is provided by the ``policy.client.access``
:ref:`feature <config_features>`, which pulls in ``script.client.select-access``,
``script.client.access-config``, ``script.client.access-default`` and
``script.client.apply-access`` as requirements, and
``script.client.access-flatpak``, ``script.client.access-snap`` and
``script.client.access-portal`` as optional wants. The portal script
additionally requires ``support.portal-permissionstore``.

There are two ways to extend the policy.

For rule-based decisions, declare a permission manager in
``access.permission-managers`` and select it from ``access.rules``; no code is
needed. See :ref:`config_access`.

For decisions that need real logic, write a hook on the ``select-access`` event
that runs before ``client/find-default-access`` and sets the
``permission-manager`` event data if — and only if — it is still unset:

.. code-block:: lua

   my_pm = PermissionManager ()
   my_pm:set_default_permissions (Perm.RX)
   my_pm:add_interest_match_simple (Perm.NONE,
     Interest {
       type = "node",
       Constraint { "media.class", "=", "Audio/Source" },
     }
   )

   SimpleEventHook {
     name = "client/find-my-access",
     before = "client/find-default-access",
     after = "client/find-config-access",
     interests = {
       EventInterest {
         Constraint { "event.type", "=", "select-access" },
       },
     },
     execute = function (event)
       if event:get_data ("permission-manager") ~= nil then
         return
       end
       local client = event:get_subject ()
       if client:get_property ("my.sandbox.id") ~= nil then
         event:set_data ("permission-manager", my_pm)
       end
     end
   }:register()

See :ref:`scripting_custom_scripts` for how to install and load such a script,
and :ref:`lua_permissions_api` for the full API reference.
