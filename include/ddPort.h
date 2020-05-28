/*
Copyright (C) 2020 Pankaj Malviya

This file is part of data diode application "IN4004"

This is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/


#ifndef __DDPORT_H__
#define __DDPORT_H__

#include <iostream>
#include <rte_ether.h>
#include <rte_ethdev.h>


// Configurable number of RX/TX ring descriptors
#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024

//class ddPort;
//typedef std::map<int, ddPort*> ddPortMap;

class ddPort
{
private:
    uint16_t _portId;
    struct rte_eth_dev_info _devInfo;
    struct rte_eth_rxconf _rxqConf;
    struct rte_eth_txconf _txqConf;
    struct rte_eth_conf _localPortConf;
    struct ether_addr _ethAddr;
    struct rte_eth_dev_tx_buffer* _txBuffer;
    struct stats_ {
        uint64_t  rx;
        uint64_t  tx;
        uint64_t  txDropped;
        uint64_t  rxDropped;
    } __rte_cache_aligned;
    struct stats_ _stats;
    struct errStats_ {
        uint64_t  badDstAddr;
        uint64_t  badSrcAddr;
        uint64_t  badEthType;
        uint64_t  badSId;
    } __rte_cache_aligned;
    struct errStats_ _errStats;

public:
    struct tunnelHdr_ {
        struct ether_addr dAddr;       // Destination address
        struct ether_addr sAddr;       // Source address
        uint16_t          etherType;   // Frame type
        uint16_t          sId;         // Secure ID
    } __attribute__((__packed__));

    ddPort(uint16_t portId);
    virtual ~ddPort() {}
    void initialize();
    rte_eth_dev_info* devInfo() { return &_devInfo; }
    const char* devName() const { return _devInfo.device->name; }
    void checkLinkStatus();
    void start();
    const struct ether_addr *ethAddr() const { return &_ethAddr; }
    uint16_t portId() const { return _portId; }
    struct rte_eth_dev_tx_buffer* txBuffer() { return _txBuffer; }
    struct stats_* stats() { return &_stats; }
    uint64_t rxStats() const { return _stats.rx; }
    uint64_t txStats() const { return _stats.tx; }
    uint64_t rxDropStats() const { return _stats.rxDropped; }
    uint64_t txDropStats() const { return _stats.txDropped; }
    uint64_t errStatsBadSrcAddr() const { return _errStats.badSrcAddr; }
    uint64_t errStatsBadDstAddr() const { return _errStats.badDstAddr; }
    uint64_t errStatsBadEthType() const { return _errStats.badEthType; }
    uint64_t errStatsBadSIdr() const { return _errStats.badSId; }
    void incRxStats(uint64_t pkts) { _stats.rx += pkts; }
    void incTxStats(uint64_t pkts) { _stats.tx += pkts; }
    void incRxDropStats(uint64_t pkts) { _stats.rxDropped += pkts; }
    void incTxDropStats(uint64_t pkts) { _stats.txDropped += pkts; }
    void incErrStatsBadSrcAddr() {  _errStats.badSrcAddr++; }
    void incErrStatsBadDstAddr() {  _errStats.badDstAddr++; }
    void incErrStatsBadEthType() {  _errStats.badEthType++; }
    void incErrStatsBadSId() {  _errStats.badSId++; }

    virtual void handleRx() = 0;
    virtual void handleTx() = 0;
};

class ddCorePort : public ddPort
{
public:
    ddCorePort(uint16_t portId) : ddPort(portId) {}
    virtual ~ddCorePort() {}
};

class ddRxOnlyCorePort : public ddCorePort
{
public:
    ddRxOnlyCorePort(uint16_t portId) : ddCorePort(portId) {}
    virtual void handleRx();
    virtual void handleTx();
    virtual ~ddRxOnlyCorePort() {}
};

class ddTxOnlyCorePort : public ddCorePort
{
public:
    ddTxOnlyCorePort(uint16_t portId) : ddCorePort(portId) {}
    virtual void handleRx();
    virtual void handleTx();
    virtual ~ddTxOnlyCorePort() {}
};

class ddAccessPort : public ddPort
{
public:
    ddAccessPort(uint16_t portId) : ddPort(portId) {}
    virtual void handleRx();
    virtual void handleTx();
    virtual ~ddAccessPort() {}
};


#endif // __DDPORT_H__

