.. _main_context_api:

Context
=======
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpContext;
      WpContext -> WpMainContext;
   }

.. doxygenstruct:: WpContext

.. doxygenstruct:: WpMainContext

.. doxygengroup:: wpcontext
   :content-only:
