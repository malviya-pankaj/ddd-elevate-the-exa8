#!/bin/sh

if [[ $EUID -ne 0 ]]
then
   echo "This script must be run as root" 
   exit 1
fi

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
echo 64 > /sys/devices/system/node/node0/hugepages/hugepages-524288kB/nr_hugepages

./dpdk-devbind.py --bind vfio-pci 0000:05:00.1
./dpdk-devbind.py --bind vfio-pci 0000:05:00.2
./dpdk-devbind.py --bind vfio-pci 0000:05:00.3

insmod ./mvmdio_uio.ko

./datadiode -l 0-3 -n 4 -- -s 4096 -p 0x6 -R

