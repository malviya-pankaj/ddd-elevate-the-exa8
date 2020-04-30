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

#include <rte_log.h>
#include <rte_byteorder.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_eal.h>
#include <rte_config.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include "ddPort.h"
#include "dataDiode.h"


static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static struct rte_eth_conf portConf;

void
ddPort::checkLinkStatus()
{
#define CHECK_INTERVAL 100 // 100ms
#define MAX_CHECK_TIME 20  // 2s (20 * 100ms) in total
    uint16_t portid;
    uint8_t count, print_flag = 0;
    struct rte_eth_link link;

    std::cout << "Checking link status for port " << _portId;
    for (count = 0; count <= MAX_CHECK_TIME; count++) {
        if (dataDiodeApp::instance().forceQuit())
            return;

        bzero(&link, sizeof(link));
        rte_eth_link_get_nowait(portid, &link);
        // print link status if flag set
        if (print_flag == 1) {
            if (link.link_status)
                std::cout << "Port " << _portId << " Link Up. Speed "
                          << link.link_speed << " Mbps - "
                          << ((link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                                 ("full-duplex") : ("half-duplex"))
                          << std::endl;
            else
                std::cout << "Port " << _portId << " Link Down" << std::endl;
                continue;
        }

        std::cout << ".";
        rte_delay_ms(CHECK_INTERVAL);

        // set the print_flag if all ports up or timeout
        if (count == (MAX_CHECK_TIME - 1)) {
            print_flag = 1;
            std::cout << "done" << std::endl;
        }
    }
}

//static uint32_t rxQueuePerLcore = 1;

ddPort::ddPort(uint16_t portId) :
        _portId(portId), _txBuffer(NULL)
{
    portConf.rxmode.split_hdr_size = 0;
    portConf.rxmode.ignore_offload_bitfield = 1;
    portConf.rxmode.offloads = DEV_RX_OFFLOAD_CRC_STRIP;
    portConf.txmode.mq_mode = ETH_MQ_TX_NONE;

    bzero(&_devInfo, sizeof(struct rte_eth_dev_info));
    bzero(&_rxqConf, sizeof(struct rte_eth_rxconf));
    bzero(&_txqConf, sizeof(struct rte_eth_txconf));
    bzero(&_localPortConf, sizeof(struct rte_eth_conf));
    bzero(&_ethAddr, sizeof(struct ether_addr));
    bzero(&_stats, sizeof(struct stats_));

    // get device info while creating ddPort object
    rte_eth_dev_info_get(portId, &_devInfo);
}

void
ddPort::initialize()
{
    std::cout << "Initializing port " << _portId
                      << " ..." << std::endl;

    if (_devInfo.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
        portConf.txmode.offloads |=
            DEV_TX_OFFLOAD_MBUF_FAST_FREE;
    int ret = rte_eth_dev_configure(_portId, 1, 1, &portConf);
    if (ret < 0)
        rte_exit(EXIT_FAILURE,
                 "Cannot configure device: err=%d, port=%u\n",
                 ret, _portId);

    ret = rte_eth_dev_adjust_nb_rx_tx_desc(_portId, &nb_rxd, &nb_txd);
    if (ret < 0)
        rte_exit(EXIT_FAILURE,
                 "Cannot adjust number of descriptors: err=%d, port=%u\n",
                 ret, _portId);

    rte_eth_macaddr_get(_portId, &_ethAddr);

    // init one RX queue
    struct rte_eth_rxconf rxqConf = _devInfo.default_rxconf;

    rxqConf.offloads = portConf.rxmode.offloads;
    ret = rte_eth_rx_queue_setup(_portId, 0, nb_rxd,
                                 rte_eth_dev_socket_id(_portId),
                                 &rxqConf,
                                 dataDiodeApp::instance().pktMbufPool());
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Port rx queue setup failed :err=%d, port=%u\n",
                 ret, _portId);

    // init one TX queue on each port
    struct rte_eth_txconf txqConf = _devInfo.default_txconf;
    txqConf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
    txqConf.offloads = portConf.txmode.offloads;
    ret = rte_eth_tx_queue_setup(_portId, 0, nb_txd,
                                 rte_eth_dev_socket_id(_portId),
                                 &txqConf);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Port tx queue setup failed :err=%d, port=%u\n",
            ret, _portId);

    // Initialize TX buffers
    _txBuffer = (rte_eth_dev_tx_buffer*)rte_zmalloc_socket("tx_buffer",
                                   RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST),
                                   0, rte_eth_dev_socket_id(_portId));
    if (_txBuffer == NULL)
        rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
                 _portId);

    rte_eth_tx_buffer_init(_txBuffer, MAX_PKT_BURST);

    ret = rte_eth_tx_buffer_set_err_callback(_txBuffer,
                                             rte_eth_tx_buffer_count_callback,
                                             &_stats.txDropped);
    if (ret < 0)
        rte_exit(EXIT_FAILURE,
                 "Cannot set error callback for tx buffer on port %u\n",
                 _portId);
    start();
}

void
ddPort::start()
{
    int ret = rte_eth_dev_start(_portId);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Ethernet device start failed: err=%d, port=%u\n",
                 ret, _portId);
}

void
ddRxOnlyCorePort::handleRx()
{
    struct rte_mbuf *pktsBurst[MAX_PKT_BURST];
    uint32_t nRx = rte_eth_rx_burst(portId(), 0,
                                    pktsBurst, MAX_PKT_BURST);

    incRxStats(nRx);

    const struct ether_addr *destAddr= dataDiodeApp::instance().corePortEthAddr();
    if (NULL == destAddr) {
        rte_exit(EXIT_FAILURE,
                 "Unable to fetch MAC address of peer core Port. Exiting...\n");
    }
    for (uint32_t j = 0; j < nRx; j++) {
        struct rte_mbuf * pkt = pktsBurst[j];
        rte_prefetch0(rte_pktmbuf_mtod(pkt, void *));
        struct tunnelHdr_ *tunnelHdr = rte_pktmbuf_mtod(pkt, struct tunnelHdr_ *);

        // Verify the encapsulation of the received packet
        uint32_t ret = memcmp(destAddr, &(tunnelHdr->dAddr), sizeof(struct ether_addr));
        if (0 == ret) {
            std::cerr << "Received packet with incorrect DST MAC." << std::endl;
        }
        const struct ether_addr* peerCorePortEthAddr = dataDiodeApp::instance().peerCorePortEthAddr();
        ret = memcmp(peerCorePortEthAddr, &(tunnelHdr->sAddr), sizeof(struct ether_addr));
        if (0 == ret) {
            std::cerr << "Received packet with incorrect SRC MAC." << std::endl;
        }
        if (tunnelHdr->etherType != rte_cpu_to_be_16(DATADIODE_TUNNEL_ETHTYPE) ||
            tunnelHdr->sId != rte_cpu_to_be_16(dataDiodeApp::instance().peerSId())) {
            std::cerr << "Incorrect EtherType or Sid detected." << std::endl;
        }
        // De-capsulate packet and transmit it on access port
        struct ether_hdr* ethHdr = reinterpret_cast<struct ether_hdr*>(rte_pktmbuf_adj(pkt, sizeof(struct ether_hdr)));
        // TODO: Add validations to validate inner frame
        struct rte_eth_dev_tx_buffer* txBuf = dataDiodeApp::instance().accessPort()->txBuffer();
        uint16_t sent = rte_eth_tx_buffer(dataDiodeApp::instance().accessPort()->portId(),
                                          0, txBuf, pkt);
        dataDiodeApp::instance().accessPort()->incTxStats(sent);
    }
}

void
ddRxOnlyCorePort::handleTx()
{
    // If a packet reaches for Tx on Rx-Only coreport, it may be suspicious
    // Report it and drop the packet
    if (txBuffer()->length) {
        std::cerr << "WARNING: Packet for Tx on Rx-Only Core port. Dropping the packet" << std::endl;
        rte_eth_tx_buffer_count_callback(txBuffer()->pkts, txBuffer()->length, &(stats()->txDropped));
    }
}

void
ddTxOnlyCorePort::handleRx()
{
    // Shouldn't receive anything on this port, drop it if anything is received
    if (txBuffer()->length) {
        std::cerr << "WARNING: Packet Rx on Tx-Only Core port. Dropping the packet" << std::endl;
        rte_eth_tx_buffer_count_callback(txBuffer()->pkts, txBuffer()->length, &(stats()->rxDropped));
    }
}

void
ddTxOnlyCorePort::handleTx()
{
    uint32_t sent = rte_eth_tx_buffer_flush(portId(), 0, txBuffer());
    if (sent) {
        incTxStats(sent);
    }
}

void
ddAccessPort::handleRx()
{
    struct rte_mbuf *pktsBurst[MAX_PKT_BURST];
    uint32_t nRx = rte_eth_rx_burst(portId(), 0,
                                    pktsBurst, MAX_PKT_BURST);

    incRxStats(nRx);

    const struct ether_addr *dstAddr= dataDiodeApp::instance().peerCorePortEthAddr();
    const struct ether_addr *srcAddr= dataDiodeApp::instance().corePortEthAddr();
    if (NULL == dstAddr) {
        rte_exit(EXIT_FAILURE,
                 "Unable to fetch MAC address of peer core Port. Exiting...\n");
    }
    for (uint32_t j = 0; j < nRx; j++) {
        struct rte_mbuf * pkt = pktsBurst[j];
        rte_prefetch0(rte_pktmbuf_mtod(pkt, void *));

        // if corePort is configured for Tx-Only role, forward the packet
        // original packet is tunneled under an l2 encapsulation
        // <DMAC 6B|SMAC 6B|ETYPE (4004) 4B|SID 4B|ORIGINALPKT|FCS>
        // DMAC: Destination MAC address
        // SMAC: Source MAC address
        // ETYPE : Ethertype set to 0x4004 (Unregistered with IANA)
        // SID: Secure ID of the Tx-only device
        if (dataDiodeApp::PORTMODE_TX == dataDiodeApp::instance().corePortMode()) {
            struct tunnelHdr_ *tunnelHdr = reinterpret_cast<struct tunnelHdr_*>(
                        rte_pktmbuf_prepend(pkt, sizeof(struct tunnelHdr_)));
            if (NULL == tunnelHdr) {
                std::cerr << "Not enough memory to send packet" << std::endl;
                return;
            }
            ether_addr_copy(dstAddr, &tunnelHdr->dAddr);
            ether_addr_copy(srcAddr, &tunnelHdr->sAddr);
            tunnelHdr->etherType = rte_be_to_cpu_16(DATADIODE_TUNNEL_ETHTYPE);
            tunnelHdr->sId = rte_be_to_cpu_16(dataDiodeApp::instance().sId());

            // put the packet into the tx buffer of core port
            struct rte_eth_dev_tx_buffer* txBuf = dataDiodeApp::instance().corePort()->txBuffer();
            uint16_t sent = rte_eth_tx_buffer(dataDiodeApp::instance().corePortId(),
                                              0, txBuf, pkt);
            dataDiodeApp::instance().corePort()->incTxStats(sent);
        } else if (dataDiodeApp::PORTMODE_RX == dataDiodeApp::instance().corePortMode()) {
            // if corePort is configured for Rx-Only role, drop the packet
            rte_pktmbuf_free(pkt);
            incRxDropStats(1);
        }
    }
}

void
ddAccessPort::handleTx()
{
    uint32_t sent = rte_eth_tx_buffer_flush(portId(), 0, txBuffer());
    if (sent) {
        incTxStats(sent);
    }
}

