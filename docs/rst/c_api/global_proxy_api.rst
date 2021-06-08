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

.. doxygenstruct:: _WpGlobalProxyClass

.. doxygengroup:: wpglobalproxy
   :content-only:
