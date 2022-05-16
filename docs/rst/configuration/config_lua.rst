.. _config_lua:

LUA Configuration Files
=======================

LUA configuration files are similar to the main configuration file, but they
leverage the LUA language to enable advanced configuration of module arguments
and allow split-file configuration.

There is only one global section that WirePlumber reads from these files: the
**components** table. This table is equivalent to the **context.components**
object on the main configuration file. Its purpose is to list components that
WirePlumber should load on startup.

Every line on the **components** table should be another table that contains
information about the loaded component::

  {
    "component-name",
    type = "component-type",
    args = { additional arguments },
    optional = true/false,
  }

* **component-name**: Should be the name of the component to load
  (ex. *"libwireplumber-module-mixer-api"*).

* **component-type**: Should be the type of the component.
  Valid component types include:

  * ``module``: A WirePlumber shared object module.
  * ``script/lua``: A WirePlumber LUA script.
  * ``pw_module``: A PipeWire shared object module (loaded on WirePlumber,
    not on the PipeWire daemon).

* **args**: Is an optional table that can contain additional arguments to be
  passed down to the module or script. Scripts can retrieve these arguments
  by declaring a line that reads ``local config = ...`` at the top of the script.
  Modules receive these arguments as a GVariant ``a{sv}`` table.

* **optional**: Is a boolean value that specifies whether loading of this
  component is optional. The default value is ``false``. If set to ``true``,
  then WirePlumber will not fail loading if the component is not found.

Split-File Configuration
------------------------

When a LUA configuration file is loaded, the engine also looks for additional
files in a directory that has the same name as the configuration file and a
``.d`` suffix.

A LUA directory can contain a list of LUA configuration files. Those files are
loaded alphabetically by filename so that user can control the order in which
LUA configuration files are executed.

LUA files in the directory are always loaded *after* the configuration file
that is out of the directory. However, it is perfectly valid to not have any
configuration file out of the directory.

Example hierarchy with files both in and out of the directory
(in the order of loading)::

  config.lua
  config.lua.d/00-functions.lua
  config.lua.d/01-alsa.lua
  config.lua.d/10-policy.lua
  config.lua.d/99-misc.lua

Example hierarchy with files only in the directory
(in the order of loading)::

  config.lua.d/00-functions.lua
  config.lua.d/01-alsa.lua
  config.lua.d/10-policy.lua
  config.lua.d/99-misc.lua

Example of a file using alsa_monitor.rules in a split-file configuration:

.. code-block:: lua

  table.insert (alsa_monitor.rules, {
    matches = {
      {
        { "device.name", "matches", "alsa_card.*" },
      },
    },
    apply_properties = {
      ["api.alsa.use-acp"] = true,
    }
  })

Multi-Path Merging
------------------

WirePlumber looks for configuration files in 3 different places, as described
in the :ref:`Locations of files <config_locations>` section. When a split-file
configuration scheme is used, files will be merged from these different locations.

For example, consider these files exist on the filesystem::

  /usr/share/wireplumber/config.lua.d/00-functions.lua
  /usr/share/wireplumber/config.lua.d/01-alsa.lua
  /usr/share/wireplumber/config.lua.d/10-policy.lua
  /usr/share/wireplumber/config.lua.d/99-misc.lua
  ...
  /etc/wireplumber/config.lua.d/01-alsa.lua
  ...
  /home/user/.config/wireplumber/config.lua.d/11-policy-extras.lua

In this case, loading ``config.lua`` will result in loading these files
(in this order)::

  /usr/share/wireplumber/config.lua.d/00-functions.lua
  /etc/wireplumber/config.lua.d/01-alsa.lua
  /usr/share/wireplumber/config.lua.d/10-policy.lua
  /home/user/.config/wireplumber/config.lua.d/11-policy-extras.lua
  /usr/share/wireplumber/config.lua.d/99-misc.lua

This is useful to keep the default configuration in /usr and override it
with host-specific and user-specific parts in /etc and /home respectively.

As an exception to this rule, if the configuration path is overridden with
the ``WIREPLUMBER_CONFIG_DIR`` environment variable, then configuration files
will only be loaded from this path and no merging will happen.

Functions
---------

Because of the nature of these files (they are scripts!), it is more convenient
to manage the **components** table through functions. In the default
configuration files shipped with WirePlumber, there is a file called
``00-functions.lua`` that defines some helper functions to load components.

When loading components through these functions, *duplicate calls are ignored*,
so it is possible to call a function to load a specific component multiple times
and it will only be loaded once.

.. function:: load_module(module, args)

   Loads a WirePlumber shared object module.

   :param string module: the module name, without the "libwireplumber-module-"
      prefix (ex specify "mixer-api" to load "libwireplumber-module-mixer-api")
   :param table args: optional module arguments table

.. function:: load_optional_module(module, args)

   Loads an optional WirePlumber shared object module. Optional in this case
   means that if the module is not present on the filesystem, it will be ignored.

   :param string module: the module name, without the "libwireplumber-module-"
      prefix (ex specify "mixer-api" to load "libwireplumber-module-mixer-api")
   :param table args: optional module arguments table

.. function:: load_pw_module(module)

   Loads a PipeWire shared object module

   :param string module: the module name, without the "libpipewire-module-"
      prefix (ex specify "adapter" to load "libpipewire-module-adapter")

.. function:: load_script(script, args)

   Loads a Lua script (a functionality script, not a lua configuration file)

   :param string script: the script's filename (ex. "policy-node.lua")
   :param table args: optional script arguments table

.. function:: load_monitor(monitor, args)

   Loads a Lua monitor script. Monitors are scripts found in the ``monitors/``
   directory and their purpose is to monitor and load devices.

   :param string monitor: the scripts's name without the directory or the .lua
      extension (ex. "alsa" will load "monitors/alsa.lua")
   :param table args: optional script arguments table

.. function:: load_access(access, args)

   Loads a Lua access script. Access scripts are ones found in the ``access/``
   directory and their purpose is to manage application permissions.

   :param string access: the scripts's name without the directory or the .lua
      extension (ex. "flatpak" will load "access/access-flatpak.lua")
   :param table args: optional script arguments table
