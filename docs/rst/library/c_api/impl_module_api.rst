.. _impl_module_api:

Local Modules
=============

.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpImplModule;
   }

.. doxygenstruct:: WpImplModule
   :members:

.. doxygengroup:: wpimplmodule
   :content-only:
