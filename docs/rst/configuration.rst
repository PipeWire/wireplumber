 .. _configuration:

Configuration
=============

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load its configured components. The actual management logic is
implemented inside those components.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

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
   configuration/features.rst
   configuration/settings.rst
   configuration/locations.rst
   configuration/main.rst
   configuration/multi_instance.rst
   configuration/alsa.rst
   configuration/bluetooth.rst
   configuration/policy.rst
   configuration/access.rst
