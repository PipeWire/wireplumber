.. _live_configuration_options:

Live Configuration Options
==========================
All simple configuration options can be changed live. This is the reason they
are also called dynamic configuration options. This means when a configuration
option is changed it is applied in real time and takes effect immediately with
out needing a restart of WirePlumber, as it would need when you :ref:`Extend or
Inherit <manipulate_configuration_options>` the configuration options. However
these live configuration options will be lost when WirePlumber is restarted
unless the persistent configuration options option is enabled.

On boot up(or restart of WirePlumber) Simple configuration options are copied to
`sm-settings` `metadata <https://docs.pipewire.org/group__pw__metadata.html>`_
and `pw-metadata` API can be used to manipulate them. For example, with the
below command all the new devices will be brought up at this new volume with the
below command::

  $ pw-metadata -n sm-settings 0 device.default-input-volume 0.5 Spa:String:JSON

Persistent Configuration Options
================================
When the persistent option is enabled all the changes done via live
configuration options will be remembered across reboots or WirePlumber restarts.

`persistent.settings` is the configuration option that controls the persistency.
The default value of this option is `false`. Override this configuration option
and set it to true to enable persistency. For example, below
:ref:`override<manipulate_configuration_options>`  will switch it on

.. code-block::

  $ cat /etc/pipewire/wireplumber.conf.d/persistent.conf

  wireplumber.settings = {
    persistent.settings = true
  }

.. note::

    The live configuration of all the simple configurations are tested to the
    extent possible, there can be cases where the configuration value is changed
    but they are not applied live.


.. note::

    All the simple configuration options are not by default live, they are
    applied live in the Lua Scripts and Modules. So if a new simple
    configuration option is added by a user, then he/she also have to take care to
    apply it live.
