.. _impl_node_api:

Local Nodes
===========
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpImplNode;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpImplNode;
   }

.. doxygenstruct:: WpImplNode
   :members:

.. doxygengroup:: wpimplnode
   :content-only:
