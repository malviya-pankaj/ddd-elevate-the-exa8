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


#ifndef __DATADIODE_H__
#define __DATADIODE_H__

#include <iostream>
#include <map>
#include <rte_ether.h>



// Max size of a single packet
#define MAX_PACKET_SZ           2048

//Size of the data buffer in each mbuf
#define MBUF_DATA_SZ            (MAX_PACKET_SZ + RTE_PKTMBUF_HEADROOM)

// Number of mbufs in mempool that is created
#define NB_MBUF                 (8192 * 16)

// How many packets to attempt to read from NIC in one go
#define PKT_BURST_SZ            32

// How many objects (mbufs) to keep in per-lcore mempool cache
#define MEMPOOL_CACHE_SZ        PKT_BURST_SZ

#define MAX_PKT_BURST           32
#define BURST_TX_DRAIN_US       100 // TX drain every ~100us
#define MEMPOOL_CACHE_SIZE      256

#define DATADIODE_TUNNEL_ETHTYPE    (0x4004)

class ddPort;
typedef std::map<int, ddPort*> ddPortMap;

class dataDiodeApp
{
public:
    enum PortMode {
        PORTMODE_NONE,
        PORTMODE_RX,
        PORTMODE_TX,
        PORTMODE_BIDIR,
        PORTMODE_INVALID  // Always a last entry
    };

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16

    struct lcoreQueueConf {
        uint32_t nRxPort;
        uint32_t rxPortList[MAX_RX_QUEUE_PER_LCORE];
    } __rte_cache_aligned;

private:
    dataDiodeApp();
    dataDiodeApp(const dataDiodeApp &obj);

    // signal handler for app
    static void sigHandler(int sigNumber)
    {
        std::cout << "Caught interrupt: " << sigNumber << " Quitting..." << std::endl;
        _forceQuit = true;
    }

    static dataDiodeApp *_appPtr;
    volatile static bool _forceQuit;

    volatile PortMode _corePortMode;
#ifndef _DD_TESTMODE_
    ddPort *_corePort;
    int _corePortId;
    struct ether_addr _peerCorePortEthAddr;
#else
    ddPort *_corePort[2];
    int _corePortId[2];
    struct ether_addr _peerCorePortEthAddr[2];
#endif

    ddPort *_accessPort;
    uint64_t _userPortMask;
    ddPortMap _pMap;
    struct rte_mempool *_pktMbufPool;
    uint32_t _nbMbufs;
    struct lcoreQueueConf _lcoreQueueConf[RTE_MAX_LCORE];
    uint16_t _sId;
    uint16_t _peerSId;
    uint64_t _timerPeriod;

protected:

public:
    void mainLoop();
    // member function to parse user arguments
    int parseArgs(int argc, char **argv);

    // member function to initialize data diode application
    void initialize(int argc, char **argv);

    // member function to cleanup the application before exiting
    void cleanup();

#ifndef _DD_TESTMODE_
    // member function to configure parameters
    void configure(struct ether_addr* peerMac, uint16_t peerSId, uint16_t sId);
#else
    // member function to configure parameters
    void configure(struct ether_addr* peerMac, struct ether_addr* peerMac1, uint16_t peerSId, uint16_t sId);
#endif

    static dataDiodeApp& instance()
    {
        if (NULL == _appPtr) {
            _appPtr = new dataDiodeApp;
        }
        return *_appPtr;
    }

    // Print out statistics on packets dropped
    void printStats();

    // Display usage
    void usage(const char *prgName);

    // accessors
    bool forceQuit() { return _forceQuit; }
    struct rte_mempool* pktMbufPool() { return _pktMbufPool; }
#ifndef _DD_TESTMODE_
    ddPort* corePort() const { return _corePort; }
    const struct ether_addr* corePortEthAddr() const;
    const struct ether_addr* peerCorePortEthAddr() { return &_peerCorePortEthAddr; }
#else
    ddPort* corePort(PortMode mode = PORTMODE_TX) const { return mode == PORTMODE_RX? _corePort[0] : _corePort[1]; }
    const struct ether_addr* corePortEthAddr(uint16_t portId) const;
    const struct ether_addr* peerCorePortEthAddr(uint16_t idx) { return &_peerCorePortEthAddr[idx]; }
#endif
    ddPort* accessPort() const { return _accessPort; }
#ifndef _DD_TESTMODE_
    const uint16_t corePortId() const { return _corePortId; }
#endif
    const PortMode corePortMode() const { return _corePortMode; }
    const uint16_t sId() const { return _sId; }
    const uint16_t peerSId() const { return _peerSId; }
};


#endif // __DATADIODE_H__

