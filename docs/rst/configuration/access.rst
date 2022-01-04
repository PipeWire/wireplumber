.. _config_access:

Access configuration
====================

main.lua.d/50-default-access-config.lua
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using a similar format as the :ref:`ALSA monitor <config_alsa>`, this
configuration file is charged to configure the client objects created by
PipeWire.

* *default_access.properties*

  A Lua object that contains generic client configuration properties in the
  for of key pairs.

  Example:

  .. code-block:: lua

    default_access.properties = {
      ["enable-flatpak-portal"] = true,
    }

  The above example sets to ``true`` the ``enable-flatpak-portal`` property.

  The list of valid properties are:

  .. code-block:: lua

    ["enable-flatpak-portal"] = true,

  Whether to enable the flatpak portal or not.

* *default_access.rules*

  This is a Lua array that can contain objects with rules for a client object.
  Those Lua objects have 2 properties. Similar to the
  :ref:`ALSA configuration <config_alsa>`, the first property is ``matches``,
  which allow users to define rules to match a client object.
  The second property is ``default_permissions``, and it is used to set
  permissions on the matched client object.

  Example:

  .. code-block:: lua

    {
      matches = {
        {
          { "pipewire.access", "=", "flatpak" },
        },
      },
      default_permissions = "rx",
    }

  This grants read and execute permissions to all clients that have the
  ``pipewire.access`` property set to ``flatpak``.

  Possible permissions are any combination of ``r``, ``w`` and ``x`` for read,
  write and execute; or ``all`` for all kind of permissions.

