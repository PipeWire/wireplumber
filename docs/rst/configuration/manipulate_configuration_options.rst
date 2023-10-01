.. _manipulate_configuration_options:

Default Configuration Options
=============================

The default configuration files are generally installed by distribution
in ``/usr/share/pipewire``. It can change in specific cases. Check
:ref:`locations <config_locations>` for more info.

The default :ref:`simple configuration options<configuration_option_types>`  are commented, this is because
all these default configuration options are already built in to WirePlumber. WirePlumber can
work with out configuration options. Uncommenting the default configuration options and assigning the
desired values will also help Override the default configuration options, however these
customizations will be lost when WirePlumber is upgraded to the newer version
and so it is recommended to override/extend in host-specific or user-specific
locations.

The default :ref:`complex configuration options<configuration_option_types>` are
not commented and WirePlumber loads them from these configuration files.
These configuration options can also be edited(overridden or extended) in
default locations,however these customizations will be lost when WirePlumber is
upgraded to the newer version and so it is recommended to override/extend in
host-specific or user-specific locations.

Override/Extend Configuration Options
=====================================
WirePlumber users interested in running with customized configuration,
can either override or extend the default configuration options, with out
touching the upstream/base configuration files.

WirePlumber will have to be restarted after these changes. The configuration
option changes can also be applied :ref:`live <live_configuration_options>`
without a restart.

Definition
----------
Overriding/Extending is the process of editing or customizing the default configuration options values
with out touching the default upstream configuration files. So even if WirePlumber is
upgraded the customized configuration options are not lost.

Overriding is completely updating the upstream configuration option value with a
new value, where as Extending is appending a value to the existing value set.
The former applies to the :ref:Simple Configuration
Options<configuration_option_types> where as later applies to :ref:`complex
configuration options<configuration_option_types>` .

Overriding :ref:`Simple Configuration Options<configuration_option_types>`
--------------------------------------------------------------------------

Assuming the Distribution/Upstream configuration files are present in
`/usr/share/pipewire`. Let's see how to override `device default volume`
configuration option, this is how the default configuration looks
like

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
- In this file define the `same configuration` under the
  `wireplumber.settings` JSON section and assign the new value.
- Restart wireplumber

This is how a overridden configuration looks like

.. code-block::


  $ cat /etc/pipewire/wireplumber.conf.d/device.conf
  ## The WirePlumber device configuration

  wireplumber.settings = {
    ## device default volume level
    device.default-volume = 0.5
  }

.. note::

    Unlike the default configuration options overridden configuration
    should be uncommented.

Overriding :ref:`Complex Configuration Options<configuration_option_types>`
---------------------------------------------------------------------------
`monitor.alsa.midi.node-properties` is a an example of complex configuration
options. Below is how to override it.

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
- In this file define the `same configuration` with a prefix of
  ``override``. and assign the new values to it.
- Restart wireplumber

The below file will override the default `monitor.alsa.midi.node-properties`

.. code-block::

 $ cat /etc/pipewire/wireplumber.conf.d/alsa.conf
 override.monitor.alsa.midi.node-properties = {
   node.name = "new-name"
   api.alsa.disable-longname = false
 }

Eventually the value of the configuration will be overridden one

.. code-block::

 monitor.alsa.midi.node-properties = {
    node.name = "new-name"
    api.alsa.disable-longname = false
 }

.. note::

    The above examples show JSON object, the JSON array type configuration options can also be
    overridden in the same way.

In the case of nested configuration properties. Individual arrays or
objects can also be overridden by prefixing the Individual objects/arrays with
`override` key word.


Extending :ref:`Complex Configuration Options<configuration_option_types>`
--------------------------------------------------------------------------
`monitor.alsa.midi.node-properties` is a an example of complex configuration
options. Below is
how to extend it.

It looks like below in the distribution installed Location

.. code::


 $ cat /usr/share/pipewire/wireplumber.conf.d/alsa.conf
   monitor.alsa.midi.node-properties = {
     node.name = "Midi-Bridge"
     api.alsa.disable-longname = true
   }

- go to `/etc/pipewire` or `$XDG_CONFIG_DIR/pipewire`
- Create a `wireplumber.conf.d` directory, if there is already one use it.
- create a file, lets say `alsa.conf`. File name actually does not matter.
- In this file define the `same configuration` with a prefix of
  ``override.``.
- Restart wireplumber

The below file will Extend/Append the default value of
`monitor.alsa.midi.node-properties` with one more property(node.nick)

.. code-block::

 $ cat /etc/pipewire/wireplumber.conf.d/alsa.conf
   monitor.alsa.midi.node-properties = {
     node.nick = "my-Midi-Bridge"
   }

Eventually the extended value of this configuration will be union of both
the default as well as the extended values

.. code-block::

   monitor.alsa.midi.node-properties = {
     node.name = "Midi-Bridge"
     api.alsa.disable-longname = true
     node.nick = "my-Midi-Bridge"
   }

Checking Config Values after Overriding or Extending
----------------------------------------------------

WirePlumber parses the configuration options during bootup. This is when it will
override or extend the configuration options. Below is how the values can be
verified to check if they are overridden or extended properly.

Simple configuration options are always loaded into `sm-settings` metadata. so
the values can be checked with pw-metadata API, for example

.. code-block::

  $ pw-metadata -n sm-settings

Complex configuration options will have to be checked from the logs. Capture
wireplumber logs from the beginning and search in the logs with the
configuration option name to know the final value of the option.
