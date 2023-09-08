.. _persistent_configs:

persistent configs
==================
When the persistent option is enabled all the changes done through live configs
will be remembered across reboots or WirePlumber restarts.

During the bootup Simple settings are copied to `sm-settings` and `pw-metadata`
API can be used to manipulate them. For example, below command will change the
device default volume, with this::

  #all the new devices hooked to the system will be brought up at this new volume.
  $ pw-metadata -n sm-settings 0 device.default-input-volume 0.5 Spa:String:JSON



