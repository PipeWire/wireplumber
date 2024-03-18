.. _config_modifying_configuration:

Modifying configuration
=======================

WirePlumber is a heavily modular daemon that depends on its configuration
file to operate. If you were to start WirePlumber with an empty configuration
file, it would fail to start. This is why the default configuration file is
installed in the system-wide application data directory, which prevents it from
being modified by the user.

It is technically possible, if you wish, to copy the default configuration
file in one of the other :ref:`configuration search locations <config_locations>`
and modify it. However, this is **not recommended**, as it may lead to issues
when upgrading WirePlumber.

In the :ref:`Configuration file <config_conf_file>` section, we saw that
configuration files support fragments, which allow you to override or extend the
default configuration. This is the recommended way to modify the configuration.

Working with fragments
----------------------

The easiest way to add :ref:`fragments <config_conf_file_fragments>` to
modify the default configuration is to create a directory called
``~/.config/wireplumber/wireplumber.conf.d`` and place your fragments there.

All fragment files need to have the ``.conf`` extension and must be valid
SPA-JSON files. The fragments are loaded in alphanumerical order, so you can
control the order in which they are loaded by naming them accordingly. It is
recommended to use a numeric prefix for the file names, e.g.
``10-my-fragment.conf``, ``20-my-other-fragment.conf``, etc., so that you can
easily control the order in which they are loaded.

Customizing the loaded features
-------------------------------

As seen in the :ref:`Components & Profiles <config_components_and_profiles>`
section, the list of components that are loaded can be customized by enabling or
disabling :ref:`well-known features <config_features>` in the profile that is
in use by WirePlumber.

The default profile of WirePlumber is called ``main``, so a fragment that
enables or disables a specific feature in the default configuration should look
like this:

.. code-block::

   wireplumber.profiles = {
     main = {
       some.feature.name = disabled
       some.other.feature.name = required
     }
   }

Remember that features can be ``required``, ``optional`` or ``disabled``. See
the :ref:`Components & Profiles <config_components_and_profiles>` for details.

Modifying dynamic options ("settings")
--------------------------------------

As seen in the :ref:`Configuration option types <config_configuration_option_types>`
section, WirePlumber components can be partly configured with dynamic options
(referred to as "settings"). These settings can either be modified permanently
in the configuration file, or they can be modified at runtime using the
``wpctl`` command-line tool.

To modify a setting in the configuration file, you can use a fragment like this:

.. code-block::

   wireplumber.settings = {
     some.setting.name = value
   }

For example, setting the ``device.routes.default-sink-volume`` setting to
``0.5`` can be done like this:

.. code-block::

   wireplumber.settings = {
     device.routes.default-sink-volume = 0.5
   }

.. note::

   Since the configuration file is only read at startup, this will only take
   effect after restarting WirePlumber.

If you would prefer to change the setting at runtime, you can use ``wpctl`` as
follows:

.. code-block:: bash

   $ wpctl settings device.routes.default-sink-volume 0.5
   Updated setting 'device.routes.default-sink-volume' to: 0.5

The above command changes the setting immediately, but for the current
WirePlumber instance only. If you want the setting to be applied every time
WirePlumber is started, you may also use the ``--save`` option:

.. code-block:: bash

   $ wpctl settings --save device.routes.default-sink-volume 0.5
   Updated and saved setting 'device.routes.default-sink-volume' to: 0.5

This will save the setting persistently in WirePlumber's state storage.
Even though it is not in the configuration file, this saved value will be
applied automatically when WirePlumber is started.

.. attention::

   When a setting's value is saved, it will override the value from the
   configuration file. Changing the value in the configuration file will
   have no effect until the saved value is removed. Use the ``--delete``
   switch in ``wpctl`` to remove a saved value (see below).

With ``wpctl``, it is also possible to restore a setting to its default value
(taken from the schema), by using the ``--reset`` option. For example, to reset
the ``device.routes.default-sink-volume`` setting, the following command can be
used:

.. code-block:: bash

   $ wpctl settings --reset device.routes.default-sink-volume
   Reset setting 'device.routes.default-sink-volume' successfully
   $ wpctl settings device.routes.default-sink-volume
   Value: 0.064 (Saved: 0.5)

Note that the ``--reset`` option will only reset the setting to its default
value, but it will not remove the saved value from the state file. If you want
to remove the saved value, you can use the ``--delete`` option:

.. code-block:: bash

   $ wpctl settings --delete device.routes.default-sink-volume
   Deleted setting 'device.routes.default-sink-volume' successfully
   $ wpctl settings device.routes.default-sink-volume
   Value: 0.064

A list of all the available settings can be found in the :ref:`config_settings`
section.

Modifying static options
------------------------

Static options always live in their own section of the configuration file.
Sections can be of two types: either a JSON object or a JSON array.

When dealing with a **JSON object**, you can add or modify a key-value pair by
creating a fragment like this:

.. code-block::

   wireplumber.some-section = {
     some.option = new_value
   }

This is similar to what we have seen also above for modifying profile features
and settings (because both are JSON objects).

When dealing with a **JSON array**, any values that you define in a fragment
will be appended to the array. For example, to add a new rule to the
``monitor.alsa.rules`` array, you can create a fragment like this:

.. code-block::

   monitor.alsa.rules = [
     {
       matches = [
         {
           device.name = "~alsa_card.*"
         }
       ]
       actions = {
         update-props = {
           api.alsa.use-ucm  = false
         }
       }
     }
   ]

This will add a new rule to the ``monitor.alsa.rules`` array, which will
be evaluated **after** all other rules that were parsed before. This is where
the order in which fragments are loaded actually matters.

If you don't want to append a new rule, but rather override the entire array
with a new one, you can do so by using the ``override.`` prefix on the array
name:

.. code-block::

   override.monitor.alsa.rules = [
     {
       matches = [
         {
           device.name = "~alsa_card.*"
         }
       ]
       actions = {
         update-props = {
           api.alsa.use-ucm  = false
         }
       }
     }
   ]

This will now replace the entire ``monitor.alsa.rules`` array with this new one.

.. attention::

   If you want to remove a rule from the array, you will need to override the
   whole array with a new one that does not contain the rule you want to remove.
   There is no way to remove a specific element from an array using fragments.

Another thing worth remembering here is that this behavior of appending values
to arrays also works in arrays that are nested inside other arrays or objects.
For example, consider this fragment:

.. code-block::

   monitor.bluez.properties = {
     bluez5.codecs = [ sbc_xq aac ldac ]
   }

If this is the first time that the ``bluez5.codecs`` array is being defined, it
will be created with the given values. If it already exists, the given values
will be appended to the existing array. If you want to make sure that this
fragment will override the existing array, you need to use the ``override.``
prefix on the array name:

.. code-block::

   monitor.bluez.properties = {
     override.bluez5.codecs = [ sbc_xq aac ldac ]
   }

The ``override.`` prefix may also be used in JSON object keys, to override the
entire object with a new one. For example, to override the entire
``monitor.bluez.properties`` object, you can use a fragment like this:

.. code-block::

   override.monitor.bluez.properties = {
     bluez5.codecs = [ sbc_xq aac ldac ]
   }

Here, the entire ``monitor.bluez.properties`` object will be replaced with the
new one, and all previous key-value pairs configured will be discarded. This
also means that the ``bluez5.codecs`` array will be replaced with the new one
and does not require the ``override.`` prefix.

.. note::

   Even though WirePlumber uses PipeWire's syntax for configuration files, the
   ``override.`` prefix is a WirePlumber extension and does not work in
   PipeWire.

Working with rules
------------------

Some of the static option sections in the configuration file are used to define
rules that are evaluated by WirePlumber at runtime. These rules are typically
used to match objects and perform actions on them. For example, the
``monitor.alsa.rules`` section is used to define rules that are evaluated by
the ALSA monitor to match ALSA devices and update their properties.

The syntax of these rules is the same as the syntax of
`PipeWire's rules <https://gitlab.freedesktop.org/pipewire/pipewire/-/wikis/Config-PipeWire#rules>`_.

A rule is always a JSON object with two keys: ``matches`` and ``actions``. The
``matches`` key is used to define the conditions that need to be met for the
rule to be evaluated as true, and the ``actions`` key is used to define the
actions that are performed when the rule is evaluated as true.

The ``matches`` key is always a JSON array of objects, where each object
defines a condition that needs to be met. Each condition is a list of key-value
pairs, where the key is the name of the property that is being matched, and the
value is the value that the property needs to have. Within a condition, all
the key-value pairs are combined with a logical AND, and all the conditions in
the ``matches`` array are combined with a logical OR.

The ``actions`` key is always a JSON object, where each key-value pair defines
an action that is performed when the rule is evaluated as true. The action
name is specific to the rule and is defined by the rule's documentation, but
most frequently you will see the ``update-props`` action, which is used to
update the properties of the matched object.

For example:

.. code-block::

   some.theoretical.rules = [
     {
       matches = [
         {
           object.name = "my_object"
           object.profile.name = "my_profile"
         }
         {
           object.name = "other_object"
         }
       ]
       actions = {
         update-props = {
           object.tag = "matched_by_my_rule"
         }
       }
     }
   ]

This rule is equivalent to the following expression:

.. code-block:: python

   if (properties["object.name"] == "my_object" and properties["object.profile.name"] == "my_profile") or (properties["object.name"] == "other_object"):
        properties["object.tag"] = "matched_by_my_rule"

In the ``matches`` array, it is also possible to use regular expressions to match
property values. For example, to match all nodes with a name that starts with
``my_``, you can use the following condition:

.. code-block::

   matches = [
     {
       node.name = "~my_.*"
     }
   ]

The ``~`` character signifies that the value is a regular expression. The exact
syntax of the regular expressions is the POSIX extended regex syntax, as
described in the `regex (7)` man page.

In addition to regular expressions, you may also use the ``!`` character to
negate a condition. For example, to match all nodes with a name that does not
start with ``my_``, you can use the following condition:

.. code-block::

   matches = [
     {
       node.name = "!~my_.*"
     }
   ]

The ``!`` character can be used with or without a regular expression. For
example, to match all nodes with a name that is not equal to ``my_node``,
you can use the following condition:

.. code-block::

   matches = [
     {
       node.name = "!my_node"
     }
   ]
