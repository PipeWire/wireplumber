.. _port_api:

PipeWire Port
=============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpPort;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpPort;
   }

.. doxygenstruct:: WpPort
   :project: WirePlumber

.. doxygengroup:: wpport
   :project: WirePlumber
   :content-only:
