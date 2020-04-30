# Elevate the EXA8 using IN4004 - The Data Diode Application

This repository contains the code for IN4004 - Data Diode application ported for Cubro's EXA8 hardware. The application is developed as an entry for the [Elevate the EXA8](https://www.cubro.com/en/about-us/exa8-contest/) Challenge.

_Pankaj_

**Secure your most critical networks!**


This code is published under GPLv3.
Please read the COPYING document for terms and conditions of the licence.



# Introduction

IN4004 - Data Diode is a program aimed at providing unidirectional data communication
between two data networks with different security levels to ensure safety of information.

This network appication shall act as a transparent uni-directional switch between the two
data networks of different (or same) security levels to connect computing devices in the
airgap in secure manner to the other network of the organization.

More information can be obtained about data diode, airgap and unidirectional networks by
visiting https://en.wikipedia.org/wiki/Unidirectional_network

**Note:** The above mentioned link is only for reference purpose and is unrelated to this work.

This application uses DPDK (dpdk-18.05.1) to process the packets.


# Topology

```
      =======                                                           =======
    /        \          ________                     ________          /       \
   | Network  |__!AP!__| IN4004 |..!CP!..|\|..!CP!..| IN4004 |__!AP!__| Network |
   |    A     |        | RxOnly |        |/|        | TxOnly |        |    B    |
    \        /         +--------+                   +--------+         \        /
     =======                          <airgap>                           =======

   LEGEND:
      AP - Access Port
      CP - Core Port
      IN4004 - Network device running IN4004 data diode application
      RxOnly - Network device to only recieve packets from the core port
      TxOnly - Network device to only send packets to the core port
```


# Operation

Data diode application IN4004 connects to Network A on one end and to Network B via
respective access ports. Two network devices running data diode application connect to
each other via core port.

The data diode application running in Tx-Only role will recieve packets from outside network
and send it to core port after encapsulating it within ethernet header with ethertype 0x4004
(as of now an unregisteredIANA Ethertype)
Refer:
https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.xhtml#ieee-802-numbers-1

The data diode application runninng in Rx-Only role will recieve packets from core port and
decapsulate it after verifying parameters like source and destination MAC addresses, Ethertype
and the data diode application's SecureID of the network device running data diode application
in Tx-Only role. Upon decapsulation the packet will be forwarded to the access port.


# Building

Data Diode Application (IN4004) uses dpdk to process packets.
Please ensure DPDK version 18.05.1 is installed on the build machine and the DPDK development environment is set.

```
export RTE_SDK=/usr/local/share/dpdk/
export RTE_TARGET=arm64-armv8a-linuxapp-gcc

```

Ensure the compiler tool chain (A cross compiler in case necessary) to be installed on the build machine.

**Note:** The Data Diode Application is developed in C++ and you may need to adapt the dpdk development environment
to compile C++ code.

Please follow the instructions mentioned at
https://patches.dpdk.org/patch/15103/

Clone the Data Diode repository on the build machine and invoke make

```
$ make
  CXX src/dataDiode_cpp.o
  CXX src/ddPort_cpp.o
  CXX src/main_cpp.o
  LD datadiode
  INSTALL-APP datadiode
  INSTALL-MAP datadiode.map
$
```
Upon successful build, the application binary will be copied at ./build/app/ folder

```
$ ls -lh ./build/app/
total 11M
-rwxrwxr-x 1 pankaj pankaj 5.5M Apr 30 09:50 datadiode
-rw-rw-r-- 1 pankaj pankaj 4.9M Apr 30 09:50 datadiode.map

```

**Note:** SuperUser (root) permissions are needed to run Data Diode Application.

Copy the binary to /mnt/data/arm_package/ and replace /mnt/data/arm_package/run_arm.sh with one provided with this application.

**You may want to backup original /mnt/data/arm_package/run_arm.sh before replacing it.**


# Configuration

The data diode application requires some configurations. Prominent ones are
1. MAC address of Peer Core Port (type: MAC address string in format xx:xx:xx:xx:xx:xx)
configured in /etc/dataDiodeApp/peerMac.conf

```
EXAMPLE:

# ls -lh /etc/dataDiodeApp/peerMac.conf
-rw------- 1 root root 0 Apr 30 10:00 /etc/dataDiodeApp/peerMac.conf
#
#
# cat /etc/dataDiodeApp/peerMac.conf
1a:4b:01:02:2f:75
#
```

2. SecureID of the Peer Core Port (type: String corresponding to 16 bit unsigned integer)
configured in /etc/dataDiodeApp/peerSid.conf

```
EXAMPLE:

# ls -lh /etc/dataDiodeApp/peerSid.conf
-rw------- 1 root root 0 Apr 30 10:00 /etc/dataDiodeApp/peerSid.conf
#
#
# cat /etc/dataDiodeApp/peerSid.conf
14275
#
```

3. SecureID of the local Core Port (type: String corresponding to 16 bit unsigned integer)
configured in /etc/dataDiodeApp/sid.conf

```
EXAMPLE:

# ls -lh /etc/dataDiodeApp/peerSid.conf
-rw------- 1 root root 0 Apr 30 10:00 /etc/dataDiodeApp/peerSid.conf
#
#
# cat /etc/dataDiodeApp/peerSid.conf
944251
#
```

These configurtion files should be accessible only to root user for ensuring security.


# Running

The application can be invoked via the shell script ./run_arm.sh

```
#!/bin/sh

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
echo 4 > /sys/devices/system/node/node0/hugepages/hugepages-524288kB/nr_hugepages

./dpdk-devbind.py --bind vfio-pci 0000:05:00.1
./dpdk-devbind.py --bind vfio-pci 0000:05:00.2
./dpdk-devbind.py --bind vfio-pci 0000:05:00.3

insmod ./mvmdio_uio.ko

./datadiode -l 0-3 -n 4 -- -s 4096 -p 0x6 -R
```

The application takes following arguments to execute

```

    -l CORELIST: An hexadecimal bit mask of the cores to run on.

    -n NUM:      Number of memory channels per processor socket.

    -p PORTMASK  PortMask to tell application which ports to use

    -R           Starts the Data Diode Application in Rx-Only role

    -T           Starts the Data Diode Application in Tx-Only role

    -s NUMBUFS   Number of buffers to be allocated
```
**Note:** On Cubro EXA8, the Data Diode Application uses interface on 0000:05:00.2 as the Core Port


# Status of the Project

Testing of the application with user traffic on Cubro EXA8 hardware got hampered due the X1 and X2 link gone bad and unavailability of spares dues to prevailaing Lockdown due to virus outbreak.

Kindly check the open issues and commit history for updates.

