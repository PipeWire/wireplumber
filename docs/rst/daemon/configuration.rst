.. _daemon_configuration:

Configuration
=============

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load its configured components. The actual management logic is
implemented inside those components.

At startup, WirePlumber reads its configuration file (combined with all the
fragments it may have) and loads the components specified in the selected
profile. This configures the operation context. Then, the components take over
and drive the entirety of the daemon's operation.

The sections below describe in more detail the configuration file format and
the various options available.

.. toctree::
   :maxdepth: 1

   configuration/conf_file.rst
   configuration/components_and_profiles.rst
   configuration/configuration_option_types.rst
   configuration/modifying_configuration.rst
   configuration/migration.rst
   configuration/features.rst
   configuration/settings.rst
   configuration/alsa.rst
   configuration/bluetooth.rst
   configuration/access.rst
