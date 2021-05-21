.. _component_loader_api:

Component Loader
================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpPlugin;
      WpPlugin -> WpComponentLoader;
   }

.. doxygenstruct:: WpComponentLoader
   :project: WirePlumber

.. doxygenstruct:: _WpComponentLoaderClass
   :project: WirePlumber

.. doxygengroup:: wpcomponentloader
   :project: WirePlumber
   :content-only:
