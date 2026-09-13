.. _stream_link_api:

Stream Links
============
.. graphviz::
  :align: center

   digraph inheritance {
      rankdir=LR;
      GObject -> WpObject;
      WpObject -> WpStreamLink;
      WpStreamLink -> WpAudioStreamLink;
      WpStreamLink -> WpVideoStreamLink;
   }

.. doxygenstruct:: WpStreamLink

.. doxygenstruct:: WpAudioStreamLink

.. doxygenstruct:: WpVideoStreamLink

.. doxygengroup:: wpstreamlink
   :content-only:
