# WirePlumber 0.2.95

First pre-release of WirePlumber 0.3.0.
This is the first release that targets desktop use-cases. It aims to be
fully compatible with `pipewire-media-session`, while at the same time it
adds a couple of features that `pipewire-media-session` lacks, such as:

  - It makes use of session, endpoint and endpoint-stream objects
    to orchestrate the graph
  - It is configurable:
    - It supports configuration of endpoints, so that their properties
      (such as their name) can be overriden
    - It also supports declaring priorities on endpoints, so that there
      are sane defaults on the first start
    - It supports partial configuration of linking policy
    - It supports creating static node and device objects at startup,
      also driven by configuration files
  - It has the concept of session default endpoints, which can be changed
    with `wpctl` and are stored in XDG_CONFIG_DIR, so the user may change
    at runtime the target device of new links in a persistent way
  - It supports volume & mute controls on audio endpoints, which can be
    set with `wpctl`
  - Last but not least, it is extensible

Also note that this release currently breaks compatibility with AGL, since
the policy management engine received a major refactoring to enable more
use-cases, and has been focusing on desktop support ever since.
Policy features specific to AGL and other embedded systems are expected
to come back in a 0.3.x point release.


# WirePlumber 0.2.0

As shipped in AGL Itchy Icefish 9.0.0 and Happy Halibut 8.0.5


# WirePlumber 0.1.1

As shipped in AGL Happy Halibut 8.0.2


# WirePlumber 0.1.1

As shipped in AGL Happy Halibut 8.0.1


# WirePlumber 0.1.0

First release of WirePlumber, as shipped in AGL Happy Halibut 8.0.0
