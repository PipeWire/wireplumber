.. _plugin_api:

Plugins
=======
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpPlugin;
   }

.. doxygenstruct:: WpPlugin

.. doxygenstruct:: _WpPluginClass

.. doxygengroup:: wpplugin
   :content-only:
