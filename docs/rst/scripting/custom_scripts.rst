.. _scripting_custom_scripts:

Custom Scripts
==============

The locations where WirePlumber searches for scripts is explained in
:ref:`config_locations_scripts`.

Scripts are not loaded automatically; a component must be defined for them, and
this component must be included in a profile. See
:ref:`config_components_and_profiles`.

Example 1: a standalone script
------------------------------

Let's assume that ``~/.local/share/wireplumber/scripts/90-hello-world.lua``
contains the following script:

.. code-block:: lua

   log = Log.open_topic ("hello-world")
   log.info ("Hello world")

In order for it to run, we'll define a component and include it in the default
profile by including the following configuration (for example, in
``~/.config/wireplumber/wireplumber.conf.d/90-hello-world.conf``):

.. code-block::

    wireplumber.components = [
      {
        name = "90-hello-world.lua", type = script/lua
        provides = hello-world
      }
    ]

    wireplumber.profiles = {
      main = {
        hello-world = required
      }
    }

Example 2: an event hook
------------------------

A script that only runs once at startup is of limited use. Most of the logic in
WirePlumber is written as *event hooks*, which run every time something happens
in the graph. See :ref:`design_events_and_hooks` for the concepts and
:ref:`lua_events_api` for the API.

``~/.local/share/wireplumber/scripts/90-log-new-nodes.lua``:

.. code-block:: lua

   log = Log.open_topic ("s-log-new-nodes")

   SimpleEventHook {
     name = "example/log-new-nodes",
     interests = {
       EventInterest {
         Constraint { "event.type", "=", "node-added" },
         Constraint { "media.class", "#", "Stream/*" },
       },
     },
     execute = function (event)
       local node = event:get_subject ()
       log:info (node, "new stream: " ..
           tostring (node.properties ["node.name"]))
     end
   }:register ()

``~/.config/wireplumber/wireplumber.conf.d/90-log-new-nodes.conf``:

.. code-block::

    wireplumber.components = [
      {
        name = "90-log-new-nodes.lua", type = script/lua
        provides = hooks.example.log-new-nodes
      }
    ]

    wireplumber.profiles = {
      main = {
        hooks.example.log-new-nodes = required
      }
    }

.. note::

   The ``wireplumber.components.rules`` in the default configuration
   automatically add ``requires = [ support.lua-scripting ]`` to every
   ``script/lua`` component, and make components whose ``provides`` matches
   ``hooks.*`` load before the standard event source (the monitor scripts are
   excluded and load after it instead). Naming your feature ``hooks.*``
   therefore ensures the hook is registered before any events are dispatched.

Example 3: replacing a shipped hook
-----------------------------------

Registering a hook with the same name as an existing one does *not* replace it;
both will run. To replace one of WirePlumber's own hooks, disable the feature
that provides it in your profile and provide your own component instead.

For instance, ``linking/find-default-target.lua`` provides the feature
``hooks.linking.target.find-default``. To substitute your own target selection
logic:

.. code-block::

    wireplumber.components = [
      {
        name = "90-my-find-target.lua", type = script/lua
        provides = hooks.example.find-target
      }
    ]

    wireplumber.profiles = {
      main = {
        hooks.linking.target.find-default = disabled
        hooks.example.find-target = required
      }
    }

To *add* to the selection chain rather than replace part of it, leave the stock
hooks enabled and order your own hook against them with ``before`` and
``after``. ``src/scripts/linking/find-user-target.lua.example`` in the source
tree is a ready-made template for exactly this; see
:ref:`scripting_existing_scripts` for the list of hooks you can order against.

Example 4: reading configuration and settings
---------------------------------------------

Scripts read static configuration with :ref:`Conf <lua_conf_api>` and runtime
settings with :ref:`Settings <lua_settings_api>`. Static configuration is read
once at startup; settings can change while WirePlumber is running.

.. code-block:: lua

   -- static configuration, from a section in wireplumber.conf or a fragment
   config = {}
   config.rules = Conf.get_section_as_json ("example.rules", Json.Array {})

   -- a runtime setting, declared in wireplumber.settings.schema
   local enabled = Settings.get_boolean ("example.enabled")

   Settings.subscribe ("example.enabled", function ()
     enabled = Settings.get_boolean ("example.enabled")
   end)

Debugging your script
---------------------

Give your script its own log topic with ``Log.open_topic ()`` and use the
``s-`` prefix, matching the convention used by the shipped scripts. You can
then enable just your messages:

.. code-block:: console

   $ WIREPLUMBER_DEBUG=s-log-new-nodes:D wireplumber

Useful things to know while developing a script:

- ``wpctl status`` shows the current graph, which is the state your hooks are
  reacting to.
- ``Debug.dump_table ()`` pretty-prints a Lua table to the log.
- A script that does not need to run inside the daemon can be executed
  standalone against a running PipeWire with ``wpexec``, which is much faster
  to iterate on. See :ref:`resources_testing` for examples.
- If your hook does not seem to run, check that its component is actually
  loaded — a profile entry naming a feature that does not exist is silently
  ignored. Raising the log level to ``D`` shows which components are loaded.

See also :ref:`lua_events_api` for the full hook API and
:ref:`scripting_existing_scripts` for the hooks that ship with WirePlumber.
