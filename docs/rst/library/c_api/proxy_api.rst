.. _proxy_api:

PipeWire Proxy
==============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
   }

.. doxygenstruct:: WpProxy

.. doxygenstruct:: _WpProxyClass

.. doxygengroup:: wpproxy
   :content-only:
