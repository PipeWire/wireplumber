.. _access_configuration_options:

Access Configuration Options
============================

WirePlumber programmers interested in writing :ref:`modules
<understanding_wireplumber>` and :ref:`scripts <understanding_wireplumber>` can
access existing configuration options and also define new configuration options
at will.

WpSettings API helps access :ref:`simple configuration options<configuration_option_types>`.
WpSettings object offers two APIs mainly one for accessing the configuration
options and another for registering callbacks to know the changes to the
configuration options. Check WpSettings :ref:`API here <settings_api>`

WpConf API helps access :ref:`complex configuration options<configuration_option_types>` Check WpConf :ref:`API
<conf_api>` for full details.


Define Configuration Options
============================

Developers can now define their own configuration options.

<configuration name>=<value> is the syntax. Values can be simple or complex.

Simple Configuration Options
----------------------------
simple configuration options take boolean, integer or floating point values.

for example, below file creates a new boolean `custom.simple-configuration`
configuration option

.. code-block::

  $ cat /etc/pipewire/wireplumber.conf.d/custom.conf

  wireplumber.settings = {
    custom.simple-configuration = true
  }

* All the simple configuration options should be defined under wireplumber.settings JSON section.
* The file name does not matter.

Complex Configuration Options
-----------------------------
Complex configuration options take JSON array or JSON object values.

for example, below file creates a new `custom.simple-configuration-option-list`
configuration option

.. code-block::

  $ cat /etc/pipewire/wireplumber.conf.d/custom.conf
    custom.simple-configuration-option-list = {
      configuration.name = "name"
      configuration.xyz.toggle = true
    }

.. note::

    The new user defined configs are better defined in the host specific or usr
    specific locations, so that they are not lost when the WirePlumber is
    upgraded. Know more about different :ref:`locations <config_locations>` in
    which the configurations are installed.

