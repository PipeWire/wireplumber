.. _state_api:

State Storage
=============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpState;
      GObject -> WpObject -> WpStateMetadata;
   }

.. doxygenstruct:: WpState

.. doxygengroup:: wpstate
   :content-only:

.. doxygenstruct:: WpStateMetadata

.. doxygengroup:: wpstatemetadata
   :content-only:
