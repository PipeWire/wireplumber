.. _man_wpdot:

wpdot(1)
========

SYNOPSIS
--------

**wpdot** [*OPTIONS*]

DESCRIPTION
-----------

**wpdot** connects to a running PipeWire daemon and prints a graph of the
WirePlumber stream and stream-link objects in the Graphviz DOT language. The
graph can then be rendered with **dot**\ (1) or any other Graphviz tool.

The graph contains:

- one cluster per node that has streams, labeled with the node ID and
  **node.name**
- one table per stream, showing its port group, its direction and its ports.
  Output streams have a red header and input streams a green one. Regular ports
  have a white background, monitor ports a yellow one and control ports a pink
  one
- one table per stream link, listing the PipeWire links it consists of, with
  edges from the output stream ports to the input stream ports. Monitor links
  are drawn with dashed edges
- one outer cluster per **node.link-group**, grouping the nodes that share it

Nodes without streams are not shown. See :ref:`stream_api` and
:ref:`stream_link_api` for details about these objects.

OPTIONS
-------

**-o** *FILE*, **--output**\ =\ *FILE*
  Write the DOT output to *FILE* instead of standard output.

**-s**, **--show-all-properties**
  Also show the global and PipeWire object properties of each node, and the
  properties of each stream. Long values are truncated.

**-h**, **--help**
  Show help options.

EXAMPLES
--------

Render the current graph to an SVG image:

.. code-block:: console

   $ wpdot | dot -Tsvg -o graph.svg

Write the graph, including all properties, to a file:

.. code-block:: console

   $ wpdot -s -o graph.dot

ENVIRONMENT
-----------

**wpdot** honours the same environment variables as **wireplumber**\ (1),
most usefully **WIREPLUMBER_DEBUG**; see :ref:`daemon_logging`.

EXIT STATUS
-----------

**0**
  Success.

**1**
  Command line usage error, the output file could not be opened, or the stream
  or stream-link managers could not be activated.

**2**
  Could not connect to PipeWire.

SEE ALSO
--------

**wireplumber**\ (1), **wpctl**\ (1), **wpexec**\ (1), **pipewire**\ (1),
**dot**\ (1)

WirePlumber Documentation: https://pipewire.pages.freedesktop.org/wireplumber/
