.. _endpoint_api:

PipeWire Endpoint
=================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpEndpoint;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpEndpoint;
      WpEndpoint -> WpImplEndpoint;
   }

.. doxygenstruct:: WpEndpoint

.. doxygenstruct:: WpImplEndpoint

.. doxygengroup:: wpendpoint
   :content-only:
