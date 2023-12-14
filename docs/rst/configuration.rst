 .. _configuration:

Configuration
=============

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load the configured modules. All the rest of the logic is implemented
inside those modules.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

At startup, WirePlumber first reads its **main** configuration file.
This file configures the operation context (properties of the daemon,
modules to be loaded, etc). This file may also specify additional, secondary
configuration files which will be loaded as well at the time of parsing the
main file.

All files and modules are specified relative to their standard search locations,
which are documented later in this chapter.

.. toctree::
   :maxdepth: 1

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
