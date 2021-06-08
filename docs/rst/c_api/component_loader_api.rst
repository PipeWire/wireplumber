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

.. doxygenstruct:: _WpComponentLoaderClass

.. doxygengroup:: wpcomponentloader
   :content-only:
