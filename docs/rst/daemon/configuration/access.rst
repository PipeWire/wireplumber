.. _config_access:

Access configuration
====================

wireplumber.conf.d/access.conf
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Using a similar format as the :ref:`ALSA monitor <config_alsa>`, this
configuration file is charged to configure the client objects created by
PipeWire.

* *Settings*

  Example:

  .. code-block::

    wireplumber.settings = {
      access-enable-flatpak-portal = true
    }

  The above example sets to ``true`` the ``access-enable-flatpak-portal``
  property.

  The list of valid properties are:

  .. code-block::

    access-enable-flatpak-portal = true,

  Whether to enable the flatpak portal or not.

* *rules*

  Example::

      access = [
        {
          matches = [
            {
              pipewire.access = "flatpak"
            }
          ]
          actions = {
            update-props = {
              default_permissions = "rx"
            }
          }
        }
      ]

  This grants read and execute permissions to all clients that have the
  ``pipewire.access`` property set to ``flatpak``.

  Possible permissions are any combination of ``r``, ``w`` and ``x`` for read,
  write and execute; or ``all`` for all kind of permissions.

