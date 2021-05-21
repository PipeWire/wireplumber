.. _client_api:

PipeWire Client
===============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpClient;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpClient;
   }

.. doxygenstruct:: WpClient
   :project: WirePlumber

.. doxygengroup:: wpclient
   :project: WirePlumber
   :content-only:
