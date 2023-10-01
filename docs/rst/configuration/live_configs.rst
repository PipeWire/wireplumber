.. _live_configs:

live configs
============
All simple settings can be changed live. This is the reason they are also called
dynamic settings. This means when a config is changed it is applied in real time
and takes effect immediately with out needing a restart of WirePlumber, as it
would need when you :ref:`Extend or Inherit <manipulate_config>` the
configs. However these live configs will be lost when WirePlumber is restarted
unless the persistent configs option is enabled.

On boot up(or restart of WirePlumber) Simple settings are copied to
`sm-settings` `metadata <https://docs.pipewire.org/group__pw__metadata.html>`_
and `pw-metadata` API can be used to manipulate them. For example, with the below command
all the new devices will be brought up at this new volume with the below command::

  $ pw-metadata -n sm-settings 0 device.default-input-volume 0.5 Spa:String:JSON

Persistent Configs
==================
When the persistent option is enabled all the changes done via live configs
will be remembered across reboots or WirePlumber restarts.

`persistent.settings` is the config that controls the persistency. The default
value of this setting is `false`. Override this config and set it to true to
enable persistency. For example, below  :ref:`override<manipulate_config>`  will
switch it on

.. code-block::

  $ cat /etc/pipewire/wireplumber.conf.d/persistent.conf

  wireplumber.settings = {
    persistent.settings = true
  }
