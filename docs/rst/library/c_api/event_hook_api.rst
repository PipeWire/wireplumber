.. _event_hook_api:

Event Hooks
===========
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpEventHook;
      WpEventHook -> WpInterestEventHook;
      WpInterestEventHook -> WpSimpleEventHook;
      WpInterestEventHook -> WpAsyncEventHook;
   }

.. doxygenstruct:: WpEventHook

.. doxygenstruct:: _WpEventHookClass

.. doxygenstruct:: WpInterestEventHook

.. doxygenstruct:: _WpInterestEventHookClass

.. doxygenstruct:: WpSimpleEventHook

.. doxygenstruct:: WpAsyncEventHook

.. doxygengroup:: wpeventhook
   :content-only:
