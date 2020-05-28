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

#include <csignal>
#include <iomanip>
#include <getopt.h>
#include <stdarg.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_eal.h>
#include <rte_config.h>
#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_log.h>
#include <ddPort.h>
#include "dataDiode.h"


dataDiodeApp *dataDiodeApp::_appPtr = NULL;
volatile bool dataDiodeApp::_forceQuit = false;

static int
perCoreLoop(__attribute__((unused)) void* args)
{
    dataDiodeApp::instance().mainLoop();
    return 0;
}

dataDiodeApp::dataDiodeApp() :
#ifndef _DD_TESTMODE_
        _corePortId(0),_corePort(NULL),
#endif
        _nbMbufs(4096), _pktMbufPool(NULL),
        _userPortMask(0), _corePortMode(PORTMODE_INVALID),
        _timerPeriod(2), _accessPort(NULL),
        _sId(0), _peerSId(0),showEthStats(false),
        _rxQueuePerLcore(1)
{
    bzero(&_peerCorePortEthAddr, sizeof(_peerCorePortEthAddr));
    bzero(_lcoreQueueConf, sizeof(lcoreQueueConf));
#ifdef _DD_TESTMODE_
        _corePortId[0] = 0;
        _corePortId[1] = 0;
        _corePort[0] = NULL;
        _corePort[1] = NULL;

#endif
}

#ifndef _DD_TESTMODE_
void
dataDiodeApp::configure(struct ether_addr* peerMac, uint16_t peerSId, uint16_t sId)
{
    _peerSId = peerSId;
    _sId = sId;
    bcopy(peerMac, &_peerCorePortEthAddr, sizeof(struct ether_addr));
}
#else
void
dataDiodeApp::configure(struct ether_addr* peerMac, struct ether_addr* peerMac1,
                        uint16_t peerSId, uint16_t sId)
{
    _peerSId = peerSId;
    _sId = sId;
    bcopy(peerMac, &_peerCorePortEthAddr[0], sizeof(struct ether_addr));
    bcopy(peerMac1, &_peerCorePortEthAddr[1], sizeof(struct ether_addr));
}
#endif

void
dataDiodeApp::initialize(int argc, char **argv)
{
    // setup signal handler to start with
    signal(SIGINT, dataDiodeApp::sigHandler);
    signal(SIGTERM, dataDiodeApp::sigHandler);

    // initialize DPDK RTE EAL
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
            rte_exit(EXIT_FAILURE,
                     "EAL initialization failed with return code %d.\nExiting...\n",
                     ret);
    }
    argc -= ret;
    argv += ret;

    // parse arguments
    if (EXIT_SUCCESS != parseArgs(argc, argv)) {
        rte_exit(EXIT_FAILURE,
                 "Incorrect arguments.\nExiting...\n");
    }

    // enumerate ports
    int nPorts = rte_eth_dev_count_avail();
    if (nPorts == 0) {
            rte_exit(EXIT_FAILURE, "No Ethernet ports.\nExiting...\n");
    }

    uint16_t portId;
    unsigned int rxLcoreId = 0;
    lcoreQueueConf *qConf = NULL;

    // Initialize the port/queue configuration of each logical core
    RTE_ETH_FOREACH_DEV(portId) {
        // skip the ports that are not enabled
        if ((_userPortMask & (1 << portId)) == 0)
            continue;

        // get the lcore_id for this port
        while (0 == rte_lcore_is_enabled(rxLcoreId) ||
                _lcoreQueueConf[rxLcoreId].nRxPort == _rxQueuePerLcore) {
            rxLcoreId++;
            if (rxLcoreId >= RTE_MAX_LCORE)
                rte_exit(EXIT_FAILURE, "Not enough cores\n");
        }

        if (qConf != &_lcoreQueueConf[rxLcoreId]) {
            // Assigned a new logical core in the loop above
            qConf = &_lcoreQueueConf[rxLcoreId];
        }

        qConf->rxPortList[qConf->nRxPort] = portId;
        qConf->nRxPort++;
        std::cout << "Lcore " << rxLcoreId << ": RX port " << portId
                  << ": nRxPort " << qConf->nRxPort << std::endl;
    }


    // create mbuf pool
    _pktMbufPool = rte_pktmbuf_pool_create("mbuf_pool", _nbMbufs,
                                           MEMPOOL_CACHE_SZ,
                                           0, MBUF_DATA_SZ,
                                           rte_socket_id());
    if (_pktMbufPool == NULL) {
        rte_exit(EXIT_FAILURE,
                 "Could not initializing memory buffer pool.\nExiting...\n");
        return;
    }

    struct rte_eth_dev_info devInfo;
    RTE_ETH_FOREACH_DEV(portId) {
        // skip ports that are not enabled
        if ((_userPortMask & (1 << portId)) == 0)
            continue;

        ddPort *pPort;

#ifndef _DD_TESTMODE_
        if (portId == _corePortId) {
            if (PORTMODE_RX == _corePortMode) {
                std::cout << "Setting Rx-Only role on Port: "
                          << portId << std::endl;
                pPort = new ddRxOnlyCorePort(portId);
                _corePort = pPort;
            } else if (PORTMODE_TX == _corePortMode) {
                std::cout << "Setting Tx-Only role on Port: "
                          << portId << std::endl;
                pPort = new ddTxOnlyCorePort(portId);
                _corePort = pPort;
            } else {
                rte_exit(EXIT_FAILURE, "Invalid Port mode %u for port: %u.\nExiting...\n",
                         _corePortMode, portId);
            }
#else
            std::cerr << "WARNING: Running the Application in test mode" << std::endl;
            std::cerr << "WARNING: Test mode is not to be run in production network" << std::endl;
            std::cout << "Setting Rx-Only and Tx-only roles on Port: "
                      << _corePortId[0] << " and Port: " << _corePortId[1]
                      << std::endl;
            if (portId == _corePortId[0]) {
                    pPort = new ddRxOnlyCorePort(portId);
                    _corePort[0] = pPort;
            } else if (portId == _corePortId[1]) {
                    pPort = new ddTxOnlyCorePort(portId);
                    _corePort[1] = pPort;
#endif
        } else {
            std::cout << "Setting Access port " << portId << std::endl;
            pPort = new ddAccessPort(portId);
            if (NULL == _accessPort) {
                _accessPort = pPort;
            }
        }
        _pMap.insert(std::pair<uint16_t,ddPort*>(portId, pPort));
        std::cout << "Port Id: " << portId << " PortName: "
                  << pPort->devName() << std::endl;

        pPort->initialize();
        pPort->checkLinkStatus();
    }

    ret = 0;
    uint32_t lcoreId;
    // launch per-lcore initialization on every lcore
    rte_eal_mp_remote_launch(perCoreLoop, NULL, CALL_MASTER);
    RTE_LCORE_FOREACH_SLAVE(lcoreId) {
        if (rte_eal_wait_lcore(lcoreId) < 0) {
            ret = -1;
            break;
        }
    }

    for(ddPortMap::iterator it = _pMap.begin(); it != _pMap.end(); ++it) {
        rte_eth_dev_stop(it->second->portId());
        rte_eth_dev_close(it->second->portId());
    }
}

void
dataDiodeApp::mainLoop()
{
    uint32_t lCoreId = rte_lcore_id();
    dataDiodeApp::lcoreQueueConf *qConf = &_lcoreQueueConf[lCoreId];

    std::cout << "Starting main loop on core: "<< lCoreId << " ..." << std::endl;

    if (qConf->nRxPort == 0) {
        std::cout << "lcore " << lCoreId << " has nothing to do" << std::endl;
        return;
    }

    for (uint32_t i = 0; i < qConf->nRxPort; i++) {
        uint32_t portId = qConf->rxPortList[i];
        std::cout << " -- lCoreId = " << lCoreId << " PortId = "
                  << portId << std::endl;
    }

    const uint64_t drainTsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S *
                BURST_TX_DRAIN_US;
    volatile uint64_t prevTsc = 0, timerTsc = 0, curTsc, diffTsc;
    while (!_forceQuit) {
        curTsc = rte_rdtsc();

        // TX burst queue drain
        diffTsc = curTsc - prevTsc;
        if (unlikely(diffTsc > drainTsc)) {

            for (ddPortMap::iterator it = _pMap.begin();
                 it != _pMap.end(); ++it) {
                if (lCoreId == it->second->portId())
                    it->second->handleTx();
            }
        }
        // Read packet from RX queues
        for (ddPortMap::iterator it = _pMap.begin();
             it != _pMap.end(); ++it) {
            if (lCoreId == it->second->portId())
                it->second->handleRx();
        }
        // do this only on master core and if timer is enabled
        if (lCoreId == rte_get_master_lcore() && _timerPeriod > 0) {
            // advance the timer
            timerTsc += diffTsc;

            // if timer has reached its timeout
            if (unlikely(timerTsc >= (_timerPeriod * rte_get_tsc_hz()))) {
                printStats();
                // reset the timer
                timerTsc = 0;
            }
        }
        prevTsc = curTsc;
    }
}

void
dataDiodeApp::cleanup()
{
    // TODO: Future enhancements
}

void
dataDiodeApp::usage(const char *prgName)
{
    std::cout << prgName << std::endl <<
       "[EAL options] --\n"
       "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
       "  -R: start the program with core Port in RxOnly mode (MUTUALLY EXCLUSIVE with -T)\n"
       "  -s MEMBUF_SIZE: Override membuf size (DEFAULT: 4096)\n"
       "  -t PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 2 default, 86400 maximum)\n"
       "  -T: start the program with core Port in TxOnly mode (MUTUALLY EXCLUSIVE with -R)\n"
#ifdef _DD_TESTMODE_
       "  -x: start the program in test mode. NOT to be run in production network\n"
#endif
       << std::endl;
}

int
dataDiodeApp::parseArgs(int argc, char **argv)
{
    int opt, ret;
    char **argvOpt;
    int optionIndex;
    char *prgName = argv[0];
    const char shortOptions[] =
        "e"  // ethernet Stats
        "h"  // help text
        "p:"  // portmask
        "R"  // receive only mode
        "s:"  // membuf size
        "T"  // transmit only mode
        "t:"  // timer period for port statistics refresh in seconds
#ifdef _DD_TESTMODE_
        "x"  // Test mode
#endif
        ;
    const struct option longOptions[] = {
        {NULL, 0, 0, 0}
    };

    argvOpt = argv;

#ifdef _DD_TESTMODE_
    _corePortId[0] = 0;
    _corePortId[1] = 1;
    _corePortMode = PORTMODE_BIDIR;
#endif

    while ((opt = getopt_long(argc, argvOpt, shortOptions,
                  longOptions, &optionIndex)) != EOF) {

        switch (opt) {
        case 'e':
            showEthStats = true;
            break;
        case 'h':
            usage(prgName);
            rte_exit(EXIT_SUCCESS, "Exiting...\n");
            break;
        // portmask
        case 'p':
        {
            char *end = NULL;
            uint64_t pm = strtoul(optarg, &end, 16);
            if ((optarg[0] == '\0') || (end == NULL) ||
                (*end != '\0') || (pm == 0)) {
                std::cerr << "Invalid PortMask!\n";
                return -1;
            }
            _userPortMask = pm;
            break;
        }
        case 's':
        {
            char *end = NULL;
            _nbMbufs = strtoul(optarg, &end, 10);
            break;
        }
#ifndef _DD_TESTMODE_
        case 'R':
            std::cout << "Setting role as Rx Only" << std::endl;
            _corePortMode = PORTMODE_RX;
            _corePortId = 1;
            break;
        case 't':
        {
            char *end = NULL;
            _timerPeriod = strtoul(optarg, &end, 10);
            _timerPeriod = (_timerPeriod > 86400) ? 86400 : _timerPeriod;
            break;
        }
        case 'T':
            std::cout << "Setting role as Tx Only" << std::endl;
            _corePortMode = PORTMODE_TX;
            _corePortId = 1;
            break;
#else
        case 'x':
            std::cout << "Setting Test mode" << std::endl;
            _corePortId[0] = 0;
            _corePortId[1] = 1;
            _corePortMode = PORTMODE_BIDIR;
            break;
#endif // _DD_TESTMODE_
        default:
            std::cerr << "Encountered Invalid Program Argument!\n" << std::endl;
            break;
        }
    }
    return EXIT_SUCCESS;
}

#ifndef _DD_TESTMODE_
const struct ether_addr*
dataDiodeApp::corePortEthAddr() const
{
    return _corePort->ethAddr();
}
#else
const struct ether_addr*
dataDiodeApp::corePortEthAddr(uint16_t portId) const
{
    return _corePort[portId]->ethAddr();
}
#endif

void
dataDiodeApp::printStats()
{
    uint64_t total_packets_dropped, total_packets_tx, total_packets_rx;
    uint16_t colWidth = 15;

    total_packets_dropped = 0;
    total_packets_tx = 0;
    total_packets_rx = 0;

    const char clr[] = { 27, '[', '2', 'J', '\0' };
    const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };

        /* Clear screen and move to top left */
    std::cout << clr << topLeft << std::endl;

    std::cout << std::endl << "Press CTRL+C to quit the program" << std::endl;
    std::cout << "===================== Data Diode IN4004 Traffic Statistics ======================"
              << std::endl
              << "Interface" << " | "
              << std::setw(colWidth) << "Packets Recvd" << " | "
              << std::setw(colWidth) << "Packets Sent" << " | "
              << std::setw(colWidth) << "Rx Dropped" << " | "
              << std::setw(colWidth) << "Tx Dropped |"
              << std::endl
              << "---------------------------------------------------------------------------------"
              << std::endl;
    for (ddPortMap::iterator it = _pMap.begin(); it != _pMap.end(); ++it) {
        std::cout << " Port "
                  << it->second->portId() << std::setw(colWidth)
                  << std::setw(5 + colWidth) << it->second->rxStats()
                  << std::setw(3 + colWidth) << it->second->txStats()
                  << std::setw(3 + colWidth) << it->second->rxDropStats()
                  << std::setw(1 + colWidth) << it->second->txDropStats()
		  << std::endl;
        }
    std::cout << std::endl
              <<"================================================================================="
              << std::endl;

    std::cout << "====================== Data Diode IN4004 Error Statistics ======================="
              << std::endl
              << "Interface" << " | "
              << std::setw(colWidth) << "Bad Src Addr" << " | "
              << std::setw(colWidth) << "Bad Dst Addr" << " | "
              << std::setw(colWidth) << "Bad Eth Type" << " | "
              << std::setw(colWidth) << "Bad SId |"
              << std::endl
              << "---------------------------------------------------------------------------------"
              << std::endl;

    for (ddPortMap::iterator it = _pMap.begin(); it != _pMap.end(); ++it) {
        std::cout << " Port "
                  << it->second->portId() << std::setw(colWidth)
                  << std::setw(5 + colWidth) << it->second->errStatsBadSrcAddr()
                  << std::setw(3 + colWidth) << it->second->errStatsBadDstAddr()
                  << std::setw(3 + colWidth) << it->second->errStatsBadEthType()
                  << std::setw(1 + colWidth) << it->second->errStatsBadSIdr()
                  << std::endl;
    }
    std::cout << std::endl
              <<"================================================================================="
              << std::endl;
    if (showEthStats) printEthStats();
}

void
dataDiodeApp::printEthStats()
{
    uint16_t colWidth = 10;
    struct rte_eth_stats ethStats;

    std::cout << "===================== Data Diode IN4004 Traffic Statistics ======================"
              << std::endl
              << "Interface" << " | "
              << std::setw(colWidth) << "ipackets" << " | "
              << std::setw(colWidth) << "opackets" << " | "
              << std::setw(colWidth) << "imissed" << " | "
              << std::setw(colWidth) << "ierrors | "
              << std::setw(colWidth) << "oerrors" << " | "
              << std::setw(colWidth) << "rx_nombuf" << " | "
              << std::setw(colWidth) << "q_ipackets" << " | "
              << std::setw(colWidth) << "q_opackets" << " | "
              << std::setw(colWidth) << "q_errors" << " |"
              << std::endl
              << "---------------------------------------------------------------------------------"
              << std::endl;
    for (ddPortMap::iterator it = _pMap.begin(); it != _pMap.end(); ++it) {
        rte_eth_stats_get(it->second->portId(), &ethStats);
        std::cout << " Port "
                  << it->second->portId() << std::setw(colWidth)
                  << std::setw(5 + colWidth) << ethStats.ipackets
                  << std::setw(3 + colWidth) << ethStats.opackets
                  << std::setw(3 + colWidth) << ethStats.imissed
                  << std::setw(3 + colWidth) << ethStats.ierrors
                  << std::setw(3 + colWidth) << ethStats.oerrors
                  << std::setw(3 + colWidth) << ethStats.rx_nombuf
                  << std::setw(3 + colWidth) << ethStats.q_ipackets[0]
                  << std::setw(3 + colWidth) << ethStats.q_opackets[0]
                  << std::setw(3 + colWidth) << ethStats.q_errors[0]
          << std::endl;
        }
    std::cout << std::endl
              <<"================================================================================="
              << std::endl;

}
