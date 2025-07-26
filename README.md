## JitterTrap

[![Coverity Status](https://scan.coverity.com/projects/4088/badge.svg)](https://scan.coverity.com/projects/4088)
[![Build Status](https://github.com/acooks/jittertrap/actions/workflows/main.yml/badge.svg)](https://github.com/acooks/jittertrap/actions/workflows/main.yml)


JitterTrap is a performance analysis tool for engineers working in the field of delay-sensitive networked applications and devices. It provides real-time measurements and network impairment emulation to facilitate development, integration and troubleshooting.

It has three broad areas of use:
* real-time analysis of network congestion;
* detection and measurement of unexpected delays, introduced by the device or application under test. That is, **characterising the source behaviour** with respect to throughput, packet rates, jitter;
* introducing and controlling known network impairment conditions (eg. delay, jitter, packet loss) to verify the correct operation of the device or application under test. That is, **characterising the behaviour of the destination**, with respect to delay, jitter and loss.

The user interface is implemented as a web application. [Have a look at the demo](http://demo.jittertrap.net) hosted on a t2.micro instance at AWS Sydney. (Performance is highly variable and may suffer if you are far away or on an impaired network :-) )


Please use GitHub Discussions for questions and suggestions.


## Installing JitterTrap

We're aiming to release packages for Fedora, Ubuntu and OpenWRT and **would appreciate help with that**.

## Building JitterTrap
### Dependencies
* [libnl](https://www.infradead.org/~tgr/libnl/) >= 3.2.24
* [libwebsockets](https://libwebsockets.org/index.html) >= 1.6
* [libjansson](http://www.digip.org/jansson/) >= 2.6

#### Fedora  

Build dependencies:  

    sudo dnf install libnl3-devel jansson-devel libwebsockets-devel libpcap-devel

Run-time dependencies:

    sudo dnf install libnl3 jansson libwebsockets libpcap


#### Ubuntu  

Build dependencies:

    sudo apt-get install libnl-3-dev libnl-route-3-dev libnl-genl-3-dev libjansson-dev libwebsockets-dev libncurses5-dev libpcap-dev pkgconf

Run-time dependencies:

    sudo apt-get install libnl-3-200 libnl-route-3-200 libnl-genl-3-200 libjansson4 libwebsockets6

### Compiling JitterTrap

Fetch:

    git clone https://github.com/acooks/jittertrap.git

Build:

    cd jittertrap
    make

Run:

    sudo ./server/jt-server --port 8080 --resource_path html5-client/output/

Now point your web browser to the user interface, eg. http://localhost:8080/
