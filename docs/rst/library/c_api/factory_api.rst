.. _factory_api:

PipeWire Factory
================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpFactory;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpFactory;
   }

.. doxygenstruct:: WpFactory

.. doxygengroup:: wpfactory
   :content-only:
