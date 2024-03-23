.. _config_migration:

Migrating configuration from 0.4
================================

The configuration file format has changed in version 0.5. No automatic migration
of old configuration files is performed, so you will have to manually update
them. This document describes the changes and how to update your configuration.

wireplumber.conf
----------------

In WirePlumber 0.4, there used to be a ``.conf`` file, typically
``wireplumber.conf``, using the SPA-JSON format, that would list some Lua
scripts in the ``wireplumber.components`` section. These scripts were of type
``config/lua`` and they were called by default ``main.lua``, ``policy.lua`` and
``bluetooth.lua``.

Typical ``wireplumber.components`` section of a ``wireplumber.conf`` file in 0.4
would look like this:

.. code-block::

   wireplumber.components = [
     #{ name = <component-name>, type = <component-type> }
     #
     # WirePlumber components to load
     #

     # The lua scripting engine
     { name = libwireplumber-module-lua-scripting, type = module }

     # The lua configuration file(s)
     # Other components are loaded from there
     { name = main.lua, type = config/lua }
     { name = policy.lua, type = config/lua }
     { name = bluetooth.lua, type = config/lua }
   ]

These Lua "configuration" scripts were then looked up in the standard
configuration directories (``/usr/share/wireplumber``, ``/etc/wireplumber`` and
``~/.config/wireplumber``). The system also supported fragments of these scripts
to be placed in directories called ``main.lua.d``, ``policy.lua.d`` and
``bluetooth.lua.d`` respectively, in the same locations.

.. attention::

   Starting with WirePlumber 0.5, Lua "configuration" files are **no longer
   supported**.

If you attempt to start it with a ``wireplumber.conf`` that still
lists ``config/lua`` components in its ``wireplumber.components`` section, you
will see the following error message on the output:

   Failed to load configuration: The configuration file at '...' is likely an
   old WirePlumber 0.4 config and is not supported anymore. Try removing it.

As the message says, to resolve this you should remove the old
``wireplumber.conf`` file from the designated location. This should allow the
new WirePlumber to start using the default configuration that it ships with.

Lua configuration scripts
-------------------------

If you had custom Lua configuration scripts in the standard configuration
directories, such as *"main.lua.d"*, *"policy.lua.d"* or *"bluetooth.lua.d"*,
**you need to port them**.

Locations of files
~~~~~~~~~~~~~~~~~~

The first thing you need to know is that the new files should be placed in the
``~/.config/wireplumber/wireplumber.conf.d/`` directory instead of
``~/.config/wireplumber/main.lua.d/`` and such ...

In addition, since the new files are in the SPA-JSON format, they should have
the ``.conf`` extension instead of ``.lua``.

See also :ref:`config_locations`.

Porting device/node rules
~~~~~~~~~~~~~~~~~~~~~~~~~

One of the most common use-cases for these scripts was to set up properties
for devices and nodes using rules. Here is an example of an old rules script:

.. code-block:: lua
   :caption: ~/.config/wireplumber/main.lua.d/51-alsa-pro-audio.lua

    local rule = {
      matches = {
        {
          { "device.name", "matches", "alsa_card.*" },
        },
      },
      apply_properties = {
        ["api.alsa.use-acp"] = false,
        ["device.profile"] = "pro-audio",
        ["api.acp.auto-profile"] = false,
        ["api.acp.auto-port"] = false,
      },
    }

    table.insert(alsa_monitor.rules, rule)

This equivalent of this script in the new configuration format would look like
this:

.. code-block::
   :caption: ~/.config/wireplumber/wireplumber.conf.d/51-alsa-pro-audio.conf

   monitor.alsa.rules = [
     {
       matches = [
         {
           device.name = "~alsa_card.*"
         }
       ]
       actions = {
         update-props = {
           api.alsa.use-acp = false,
           device.profile = "pro-audio"
           api.acp.auto-profile = false
           api.acp.auto-port = false
         }
       }
     }
   ]

Another example of Bluetooth node rules:

.. code-block:: lua
   :caption: ~/.config/wireplumber/bluetooth.lua.d/51-headphones.lua

    local rule = {
      matches = {
        {
          { "node.name", "equals", "bluez_output.02_11_45_A0_B3_27.a2dp-sink" },
        },
      },
      apply_properties = {
        ["node.nick"] = "Headphones",
      },
    }

    table.insert(bluez_monitor.rules, rule)

This equivalent of this script in the new configuration format would look like:

.. code-block::
   :caption: ~/.config/wireplumber/wireplumber.conf.d/51-headphones.conf

   monitor.bluez.rules = [
     {
       matches = [
         {
           node.name = "bluez_output.02_11_45_A0_B3_27.a2dp-sink"
         }
       ]
       actions = {
         update-props = {
           node.nick = "Headphones"
         }
       }
     }
   ]

See also :ref:`config_modifying_configuration_rules`.

Porting properties configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you had configuration scripts that were setting properties in tables such
as ``alsa_monitor.properties`` or ``bluez_monitor.properties``, then in many
cases porting to the new format can be done as follows:

.. code-block:: lua
   :caption: ~/.config/wireplumber/bluetooth.lua.d/80-bluez-properties.lua

    bluez_monitor.properties["bluez5.roles"] = "[ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]"
    bluez_monitor.properties["bluez5.hfphsp-backend"] = "native"

.. code-block::
   :caption: ~/.config/wireplumber/wireplumber.conf.d/80-bluez-properties.conf

    monitor.bluez.properties = {
      bluez5.roles = [ a2dp_sink a2dp_source bap_sink bap_source hsp_hs hsp_ag hfp_hf hfp_ag ]
      bluez5.hfphsp-backend = "native"
    }

See also :ref:`config_modifying_configuration_static`.

In a lot of cases, however, these properties have been promoted to become either
:ref:`Settings <config_modifying_configuration_settings>` or
:ref:`Features <config_modifying_configuration_features>`.
Here are some common examples:

Disabling the D-Bus device reservation API in the ALSA monitor:

* Old format:

  .. code-block:: lua
     :caption: ~/.config/wireplumber/main.lua.d/80-disable-alsa-reserve.lua

      alsa_monitor.properties["alsa.reserve"] = false

* New format:

  .. code-block::
     :caption: ~/.config/wireplumber/wireplumber.conf.d/80-disable-alsa-reserve.conf

      wireplumber.profiles = {
        main = {
          monitor.alsa.reserve-device = disabled
        }
      }

Disabling seat monitoring via logind in the BlueZ monitor:

* Old format:

  .. code-block:: lua
     :caption: ~/.config/wireplumber/bluetooth.lua.d/80-disable-logind.lua

      bluez_monitor.properties["with-logind"] = false

* New format:

  .. code-block::
     :caption: ~/.config/wireplumber/wireplumber.conf.d/80-disable-logind.conf

      wireplumber.profiles = {
        main = {
          monitor.bluez.seat-monitoring = disabled
        }
      }

See also :ref:`config_modifying_configuration_features`.

Linking policy configuration (moved to settings and renamed):

* Old format:

  .. code-block:: lua
     :caption: ~/.config/wireplumber/policy.lua.d/80-policy.lua

      default_policy.policy = {
        ["move"] = false,
        ["follow"] = false,
      }

* New format:

  .. code-block::
     :caption: ~/.config/wireplumber/wireplumber.conf.d/80-policy.conf

      wireplumber.settings = {
        linking.allow-moving-streams = false
        linking.follow-default-target = false
      }

See also :ref:`config_modifying_configuration_settings` and remember that
settings can also be changed at runtime via :command:`wpctl`.

Loading custom scripts
~~~~~~~~~~~~~~~~~~~~~~

If you had custom Lua scripts that were loaded by the old configuration file,
you need to port the old ``load_script()`` commands into component descriptions.

For example, if you had a script that was loaded like this:

.. code-block:: lua
   :caption: ~/.config/wireplumber/main.lua.d/99-my-script.lua

    load_script("my-script.lua")

You should now create a new component description in the configuration file
and also make sure to require it in the profile:

.. code-block::
   :caption: ~/.config/wireplumber/wireplumber.conf.d/99-my-script.conf

   wireplumber.components = [
     {
       name = my-script.lua, type = script/lua
       provides = custom.my-script
     }
   ]

   wireplumber.profiles = {
     main = {
       custom.my-script = required
     }
   }

.. attention::

   Another important thing to mention here is the location of custom scripts. In
   0.4, scripts could be loaded in configuration locations such as
   ``~/.config/wireplumber/scripts/`` and ``/etc/wireplumber/scripts/``. In 0.5,
   the XDG base directory specification for data files is honored, so the new
   location for custom scripts is ``~/.local/share/wireplumber/scripts/`` and
   anything else specified in ``$XDG_DATA_HOME`` and ``$XDG_DATA_DIRS``. See
   :ref:`config_locations` for more information.
