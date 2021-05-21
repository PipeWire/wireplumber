.. _global_proxy_api:

PipeWire Global Object Proxy
============================
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
   }

.. doxygenstruct:: WpGlobalProxy
   :project: WirePlumber

.. doxygenstruct:: _WpGlobalProxyClass
   :project: WirePlumber

.. doxygengroup:: wpglobalproxy
   :project: WirePlumber
   :content-only:
