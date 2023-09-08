.. _access_configs:

Access Configs
==============

WirePlumber programmers interested in writing :ref:`modules
<understanding_wireplumber>` and :ref:`scripts <understanding_wireplumber>` can
access existing configs and also define new configs at will.

wpsettings API helps access :ref:`simple configs<configs_types>`. wpsettings
object offers two APIs mainly one for accessing the configs and another for
registering callbacks to know the changes to the configs. Check wpsettings
:ref:`API here <settings_api>`

wpconf API helps access :ref:`complex configs<configs_types>` Check wpconf :ref:`API
<conf_api>` for full details.


Define Configs
==============

Developers can now define their own configs.

<configuration name>=<value> is the syntax. Values can be simple or complex.

Simple Configs
--------------
simple configs take boolean, integer or floating point values.

for example, below file creates a new boolean `custom.simple-property` config::

  $ cat /etc/pipewire/wireplumber.conf.d/custom.conf

  wireplumber.settings = {
    custom.simple-property = true
  }

* All the simple configs should be defined under wireplumber.settings JSON section.
* The file name does not matter.

Complex Configs
---------------
Complex configs take JSON array or JSON object values.

for example, below file creates a new `custom.simple-property-list` config::

  $ cat /etc/pipewire/wireplumber.conf.d/custom.conf
    custom.simple-property-list = {
      prop.name = "name"
      prop.xyz.toggle = true
    }


