.. _settings_api:

Settings
========
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GBoxed -> WpSettingsSpec;
      GBoxed -> WpSettingsItem;
      GObject -> WpObject;
      WpObject -> WpSettings;
   }

.. doxygenstruct:: WpSettingsSpec

.. doxygenstruct:: WpSettingsItem

.. doxygenstruct:: WpSettings

.. doxygengroup:: wpsettings
   :content-only:
