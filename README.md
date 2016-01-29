## JitterTrap

<a href="https://scan.coverity.com/projects/4088">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/4088/badge.svg"/>
</a>
[![Build Status](https://travis-ci.org/acooks/jittertrap.svg)](https://travis-ci.org/acooks/jittertrap)
[![Code Climate](https://codeclimate.com/github/acooks/jittertrap/badges/gpa.svg)](https://codeclimate.com/github/acooks/jittertrap)

JitterTrap is a network measurement and impairment tool for developers of real-time applications and devices.

It has two broad areas of use:
* detection and measurement of unexpected delays, introduced by the device or application under test. That is, characterising the source behaviour with respect to throughput, packet rates, jitter.
* introducing and controling known network impairment conditions (eg. delay, jitter, packet loss) to verify the correct operation of the device or application under test. That is, characterising the behaviour of the destination, with respect to delay, jitter and loss.

The user interface is implemented as a web application.  
[![Demo Video](https://img.youtube.com/vi/7u6xBxz6bFY/0.jpg)](https://youtu.be/7u6xBxz6bFY "Demo video")

## Talk to us
[![Visit our IRC channel](https://kiwiirc.com/buttons/irc.freenode.net/jittertrap.png)](https://kiwiirc.com/client/irc.freenode.net/?nick=CuriousCat|?&theme=cli#jittertrap)

Help is available at #jittertrap on irc.freenode.net or help@jittertrap.net


## Installing JitterTrap

We're working on packaging JitterTrap and its dependencies for Fedora and Ubuntu. If you'd like to help, send email to packaging@jittertrap.net.

## Building JitterTrap
### Dependencies
* [libnl](https://www.infradead.org/~tgr/libnl/) >= 3.2.24
* [libwebsockets](https://libwebsockets.org/index.html) >= 1.6
* [libjansson](http://www.digip.org/jansson/) >= 2.6

#### Fedora  
libwebsockets packaging is in progress, but in the mean time you can get it from [this copr repo](https://copr.fedoraproject.org/coprs/acooks/libwebsockets/)

    sudo dnf copr enable acooks/libwebsockets

Build dependencies:  

    sudo dnf install libnl3-devel jansson-devel libwebsockets-devel

Run-time dependencies:

    sudo yum install libnl3 jansson libwebsockets


#### Ubuntu  
libwebsockets packaging is in progress, but in the mean time you can get it from[this ppa](https://launchpad.net/~acooks/+archive/ubuntu/libwebsockets6)

    sudo apt-add-repository ppa:acooks/libwebsockets6
    sudo apt-get update

Build dependencies:

    sudo apt-get install libnl-3-dev libnl-route-3-dev libnl-genl-3-dev libjansson-dev libwebsockets-dev

Run-time dependencies:

    sudo apt-get install libnl-3-200 libnl-route-3-200 libnl-genl-3-200 libjansson4 libwebsockets6

### Compiling JitterTrap

Fetch:

    git clone https://github.com/acooks/jittertrap.git

Build:

    cd jittertrap
    make

Run:

    sudo ./server/jt-server --resource_path html5-client/output/
