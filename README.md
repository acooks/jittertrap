Toptalk is a simple top-like tool that displays the top network flows over
a configurable time interval.

A network flow is determined by  
  * source ip
  * destination ip
  * source port
  * destination port
  * protocol (eg, TCP, UDP)


The interval of flow measurement can be adjusted from 100ms to 60s, by
pressing '-' or '+'.


Dependencies:
  * ncurses
  * libpcap


## Building Toptalk

#### Ubuntu

Build-dependencies:

    sudo apt install libncurses5-dev libpcap-dev pkgconf
