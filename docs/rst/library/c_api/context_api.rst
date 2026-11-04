.. _main_context_api:

Context
=======
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpContext;
      WpContext -> WpMainContext;
      WpContext -> WpClientContext;
   }

.. doxygenstruct:: WpContext

.. doxygenstruct:: WpMainContext

.. doxygenstruct:: WpClientContext

.. doxygengroup:: wpcontext
   :content-only:
