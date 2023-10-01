.. _access:

Access Configuration
====================

The configuration file ``wireplumber.conf.d/access.conf`` is charged to
configure the permissions on client objects created by PipeWire.

Simple Configs
--------------

All the :ref:`simple configs<config_types>` can be
:ref:`overridden<manipulate_config>` or can be changed
:ref:`live<live_configs>`. They are commented in the default location as they
are built into WirePlumber. Below is the explanation of each of these simple
configs.

.. code-block::

    access-enable-flatpak-portal = true

The above example sets to ``true`` the ``access-enable-flatpak-portal``
property.

The list of valid properties are:

.. code-block::

  access-enable-flatpak-portal = true,

Whether to enable the flatpak portal or not.

Complex Configs
---------------

The :ref:`complex configs<config_types>`  can be either
:ref:`overridden<manipulate_config>`  or :ref:`extended<manipulate_config>` but they
cannot be changed :ref:`live<live_configs>`

.. code-block::

  access.rules = [
    # The following are the default rules applied if none overrides them.
    {
      matches = [
        {
          pipewire.access = "flatpak"
          media.category = "Manager"
        }
      ]
      update-props = {
        default_permissions = "all",
      }
    }
    {
      matches = [
        {
          pipewire.access = "flatpak"
        }
      ]
      update-props = {
        default_permissions = "rx"
      }
    }
    {
      matches = [
        {
          pipewire.access = "restricted"
        }
      ]
      update-props = {
        default_permissions = "rx"
      }
    }
  ]

These rules grants read and execute permissions to clients based on the value
of the ``pipewire.access`` property.

.. note::

  Possible permissions are any combination of ``r``, ``w`` and ``x`` for read,
  write and execute; or ``all`` for all kind of permissions.

.. note::

  The properties set in the update-props section, can be PipeWire properties
  which trigger some action or they can be new properties that the devices or
  nodes will be created with. These new properties can be read or written from
  scripts or modules. After the creation of the devices and nodes new
  properties cannot be created on them.

