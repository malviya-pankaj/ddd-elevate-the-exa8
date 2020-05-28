#!/bin/bash

if [ $EUID != 0 ]; then
  echo "This script must be run as root" 
  exit 1
fi

if [ x{$RTE_SDK} == "x" ]
then
  echo "Please set RTE_SDK"
  exit 1
fi

if [ x{$RTE_TARGET} == "x" ]
then
  echo "Please set RTE_TARGET"
  exit 1
fi

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
echo 16 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages

lsmod | grep -q igb_uio
if [ $? -eq 1 ]
then
  echo "Inserting igb_uio.ko..."
  modprobe uio_pci_generic
  insmod ${RTE_SDK}/${RTE_TARGET}/kmod/igb_uio.ko
else
  echo "igb_uio is running"
fi

ip link set enp0s8 down
ip link set enp0s9 down


dpdk-devbind.py --bind igb_uio 0000:00:08.0
dpdk-devbind.py --bind igb_uio 0000:00:09.0

./datadiode -l 0-1 -n 4 -- -s 4096 -p 0x7 $1

