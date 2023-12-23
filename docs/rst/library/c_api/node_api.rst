.. _node_api:

PipeWire Node
=============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpNode;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpNode;
   }

.. doxygenstruct:: WpNode

.. doxygengroup:: wpnode
   :content-only:
