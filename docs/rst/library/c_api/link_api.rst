.. _link_api:

PipeWire Link
=============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpProxy;
      WpProxy -> WpGlobalProxy;
      WpGlobalProxy -> WpLink;
      GInterface -> WpPipewireObject;
      WpPipewireObject -> WpLink;
   }

.. doxygenstruct:: WpLink

.. doxygengroup:: wplink
   :content-only:
