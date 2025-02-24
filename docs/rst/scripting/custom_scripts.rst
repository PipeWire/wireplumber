.. _scripting_custom_scripts:

Custom Scripts
==============

The locations where WirePlumber searches for scripts is explained in
:ref:`config_locations_scripts`.

Scripts are not loaded automatically; a component muse be defined for them, and
this component must be included in a profile. See
:ref:`config_components_and_profiles`.

Full example
------------

Let's assume that ``~/.local/share/wireplumber/scripts/90-hello-world.lua``
contains the following script:

.. code-block:: lua

   log = Log.open_topic("hello-world")
   log.info("Hello world")

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
