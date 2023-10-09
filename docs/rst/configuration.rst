 .. _configuration:

Configuration
=============

WirePlumber is a heavily modular daemon. By itself, it doesn't do anything
except load the configured components. The actual logic is implemented inside
those components. Modules and Lua scripts are two types of components. Modules
are written in C, they usually extent the core wireplumber(libwireplumber)
logic, whereas Lua scripts implement the actual session management logic.

Modular design ensures that it is possible to swap the implementation of
specific functionality without having to re-implement the rest of it, allowing
flexibility on target-sensitive parts, such as policy management and
making use of non-standard hardware.

At startup, WirePlumber first reads its **main** configuration file.
This file configures the operation context (properties of the daemon,
modules to be loaded, etc). This file may also specify additional, secondary
configuration files which will be loaded as well at the time of parsing the
main file.

All files and modules are specified relative to their standard search :ref:`locations<config_locations>`.

PipeWire SPA-JSON, the new configuration system
-----------------------------------------------

With 0.5 release, the configuration system is moved from Lua to PipeWire SPA-JSON.
The earlier Lua configuration files are not supported anymore. The new mechanism
aligns WirePlumber configuration system with that of PipeWire. PipeWire JSON is not
strict JSON, its a customized form of it.

The configuration options are logically grouped(alsa.conf, device.conf etc) and
maintained in different files. You can find all of them under
`wireplumber.conf.d`. These groups of configuration options are explained below.

.. note::

    Lua is still the scripting language for WirePlumber.


.. toctree::
   :maxdepth: 1

   configuration/locations.rst
   configuration/main.rst
   configuration/multi_instance.rst
   configuration/configuration_option_types.rst
   configuration/manipulate_configuration_options.rst
   configuration/access_configuration_options.rst
   configuration/live_configuration_options.rst
   configuration/alsa.rst
   configuration/bluetooth.rst
   configuration/linking.rst
   configuration/access.rst
   configuration/device.rst
   configuration/stream.rst
   configuration/filters.rst
