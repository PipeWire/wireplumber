.. _manipulate_config:

Default Configs
===============

The default config files are generally installed by distribution in
``/usr/share/pipewire``. It can change in specific cases. Check :ref:`locations
<config_locations>` for more info.

The default :ref:`simple configs<config_types>`  are commented, this is because
all these default settings are already built in to WirePlumber. WirePlumber can
work with out settings. Uncommenting the default settings and assigning the
desired values will also help Override the default configs, however these
customizations will be lost when WirePlumber is upgraded to the newer version
and so it is recommended to override/extend in host-specific or user-specific
locations.

The default :ref:`complex configs<config_types>` are not commented and
WirePlumber loads them from these config files. These configs can also be
edited(overridden or extended) in default locations,however these customizations
will be lost when WirePlumber is upgraded to the newer version and so it is
recommended to override/extend in host-specific or user-specific locations.

Override/Extend Configs
=======================
WirePlumber users interested in running with customized config, can either
override or extend the default config settings, with out touching the
upstream/base config files.

WirePlumber will have to be restarted after these changes. The config changes can
also be applied :ref:`live <live_configs>`  without a restart.

Overriding
==========
Overriding is the process of editing or customizing the default config values
with out touching the default upstream configs. So even if WirePlumber is
upgraded the custom configs are not lost.

Over Riding :ref:`Simple Configs<configs_types>`
------------------------------------------------

Assuming the Distribution/Upstream config files are present in
`/usr/share/pipewire`. Let's see how to override `device default volume` config
setting, this is how the default config looks like

.. code-block::

  $ cat /usr/share/pipewire/wireplumber.conf.d/device.conf
  ## The WirePlumber device configuration

  wireplumber.settings = {
    ## device default volume level
    # device.default-volume = 0.064
  }

- go to `/etc/pipewire` or `$XDG_CONFIG_DIR/pipewire`
- Create a `wireplumber.conf.d` directory, if there is already one, use it.
- create a file, lets say `device.conf`. File name actually does not matter.
- In this file define the `same config` under the `wireplumber.settings` JSON
  section and assign the new value.
- Restart wireplumber

This is how a overridden config looks like

.. code-block::


  $ cat /etc/pipewire/wireplumber.conf.d/device.conf
  ## The WirePlumber device configuration

  wireplumber.settings = {
    ## device default volume level
    device.default-volume = 0.5
  }

.. note::

    Unlike the default configs overridden config should be uncommented.

Overriding :ref:`Complex Configs<config_types>`
------------------------------------------------
`monitor.alsa.midi.node-properties` is a an example of complex configs. Below is
how to override it.

It looks like below in the distribution installed location

.. code-block::

 $ cat /usr/share/pipewire/wireplumber.conf.d/alsa.conf
   monitor.alsa.midi.node-properties = {
     node.name = "Midi-Bridge"
     api.alsa.disable-longname = true
   }

- go to `/etc/pipewire` or `$XDG_CONFIG_DIR/pipewire`
- Create a `wireplumber.conf.d` directory, if there is already one use it.
- create a file, lets say `alsa.conf`. File name actually does not matter.
- In this file define the `same config` with a prefix of ``override``. and
  assign the new values to it.
- Restart wireplumber

The below file will override the default `monitor.alsa.midi.node-properties`

.. code-block::

 $ cat /etc/pipewire/wireplumber.conf.d/alsa.conf
 override.monitor.alsa.midi.node-properties = {
   node.name = "new-name"
   api.alsa.disable-longname = false
 }

Eventually the value of the config will be overridden one

.. code-block::

 monitor.alsa.midi.node-properties = {
    node.name = "new-name"
    api.alsa.disable-longname = false
 }

.. note::

    The above examples show JSON object, the JSON array type config settings can
    also be overridden in the same way.

In the case of nested config properties. Individual arrays or objects can also
be overridden by prefixing the Individual objects/arrays with `override` key word.


Extending :ref:`Complex Configs<config_types>`
-----------------------------------------------
`monitor.alsa.midi.node-properties` is a an example of complex configs. Below is
how to extend it.

It looks like below in the distribution installed location ::


 $ cat /usr/share/pipewire/wireplumber.conf.d/alsa.conf
   monitor.alsa.midi.node-properties = {
     node.name = "Midi-Bridge"
     api.alsa.disable-longname = true
   }

- go to `/etc/pipewire` or `$XDG_CONFIG_DIR/pipewire`
- Create a `wireplumber.conf.d` directory, if there is already one use it.
- create a file, lets say `alsa.conf`. File name actually does not matter.
- In this file define the `same config` with a prefix of ``override.``.
- Restart wireplumber

The below file will Extend/Append the default value of
`monitor.alsa.midi.node-properties` with one more property(node.nick)

.. code-block::

 $ cat /etc/pipewire/wireplumber.conf.d/alsa.conf
   monitor.alsa.midi.node-properties = {
     node.nick = "my-Midi-Bridge"
   }

Eventually the extended value of this config will be union of both the default
as well as the extended values

.. code-block::

   monitor.alsa.midi.node-properties = {
     node.name = "Midi-Bridge"
     api.alsa.disable-longname = true
     node.nick = "my-Midi-Bridge"
   }

Checking Config Values after Overriding or Extending
----------------------------------------------------

WirePlumber parses the configs during bootup. This is when it will override or
extend the configs. Below is how the values can be verified to check if they are
overridden or extended properly.

Simple configs are always loaded into `sm-settings` metadata. so the values can
be checked with pw-metadata API, for example

.. code-block::

  $ pw-metadata -n sm-settings

Complex configs will have to be checked from the logs. Capture wireplumber logs
from the beginning and search in the logs with the config name to know the final
value of the config.
