.. _config_configuration_option_types:

Configuration option types
==========================

As seen in the previous sections, WirePlumber can be partly configured by
enabling or disabling features, which affect which components are getting
loaded. These components, however, can be further configured to fine-tune their
behavior. This section describes the different types of configuration options
that can be used to configure WirePlumber components.

Dynamic options ("Settings")
----------------------------

Dynamic options (also simply referred to as "settings") are configuration
options that can be changed at runtime. They are typically simple values like
booleans, integers, strings, etc. and are all located under the
``wireplumber.settings`` section in the configuration file. Their purpose is to
allow the user to change simple behavioral aspects of WirePlumber.

As the name suggests, these options are dynamic and can be changed at runtime
using ``wpctl`` or the :ref:`settings_api` API. For example, setting the
``device.routes.default-sink-volume`` setting to ``0.5`` can be done like this:

.. code-block:: bash

   $ wpctl settings device.routes.default-sink-volume 0.5

Under the hood, when WirePlumber starts, the ``metadata.sm-settings`` component
(provided by ``libwireplumber-module-settings``) reads this section from the
configuration file and populates the ``sm-settings`` metadata object, which is
exported to PipeWire. In addition, it reads the ``wireplumber.settings.schema``
section and populates the ``schema-sm-settings`` metadata object, which is used
by the API to validate the settings. Any options that are missing from
``wireplumber.settings`` are also populated in ``sm-settings`` from their
default values in the schema. Then the rest of the components read their
configuration options from this metadata object via the :ref:`settings_api` API.

Most of the components that use such dynamic options make sure to listen
to changes in the metadata object so that they can immediately adapt their
behavior. Other components, however, do not react immediately and the changes
only take effect the next time the option is needed. For instance, some options
affect created objects in a way that cannot be changed after the object has been
created, so when the option is changed it applies only to new objects and not
existing ones.

Changing the settings at runtime in the ``sm-settings`` metadata object is
a non-persistent change. The changes will be lost when WirePlumber is
restarted. However, the :ref:`settings_api` API also supports saving settings
to a state file, which will be loaded again when WirePlumber starts and
override the settings from the configuration file. This is done by using yet
another metadata object called ``persistent-sm-settings``. When a setting is
changed in the ``persistent-sm-settings`` metadata object, WirePlumber
automatically saves the change to the state file and also changes the value in
the ``sm-settings`` metadata object immediately.

To make such a persistent change using ``wpctl``, the ``--save`` option can be
used. For example, to set the ``device.routes.default-sink-volume`` setting to
``0.5`` and save it to the state file:

.. code-block:: bash

   $ wpctl settings --save device.routes.default-sink-volume 0.5

With ``wpctl``, it is also possible to restore a setting to its default value
(taken from the schema), by using the ``--reset`` option. For example, to reset
the ``device.routes.default-sink-volume`` setting, the following command can be
used:

.. code-block:: bash

   $ wpctl settings --reset device.routes.default-sink-volume

In addition, the ``--delete`` option can be used to delete a setting from the
``persistent-sm-settings`` metadata object, which will also remove it from the
state file. After deleting, the value from the ``wireplumber.settings`` section
of the configuration file will be used again. For example, to delete the
``device.routes.default-sink-volume`` setting, the following command can be
used:

.. code-block:: bash

   $ wpctl settings --delete device.routes.default-sink-volume

A list of all the available settings can be found in the :ref:`config_settings`
section.

Static options
--------------

Static options are more complex configuration structures that reside only in the
configuration file and cannot be changed at runtime. They are typically used to
configure device monitors and provide rules that match objects and perform
actions such as update their properties.

While these options could also in theory be stored in the metadata object and
be made dynamic, this is not supported because these options are both complex
and therefore hard to change on the command line, but also because they are
typically used to configure objects that are created at startup and cannot be
changed later.

Static options are located in their own top-level sections. Examples of such
sections are ``monitor.alsa.properties`` and ``monitor.alsa.rules`` that are
used to configure the ``monitor.alsa`` component. The next sections of this
documentation describe in detail all the available static options.

Component arguments
~~~~~~~~~~~~~~~~~~~

Components can also be configured statically by passing arguments to them when
they are loaded. This is done by adding an ``arguments`` key to the component
description in the ``wireplumber.components`` section (see
:ref:`config_components_and_profiles`).

The arguments are mostly meant as a way to instantiate multiple instances of the
same module or script with slightly different configuration to create a new
unique component. For example, the ``metadata.lua`` script can be instantiated
multiple times to create multiple metadata objects, each with a different name.
The name of the metadata object is passed as an argument to the script.

While many more static options could be passed as arguments, this is not
recommended because it is not possible to override the arguments by adding
:ref:`fragment<config_conf_file_fragments>` configuration files. Therefore, it
is recommended to use component-specific top-level sections, unless the option
is not meant to be changed by the user.
