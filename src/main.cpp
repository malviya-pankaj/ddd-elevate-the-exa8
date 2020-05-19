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

#include "dataDiode.h"
#include <vector>
#include <iostream>
#include <iterator>
#include <string>
#include <fstream>
#include <rte_ether.h>


// Main
int
main (int argc, char **argv)
{
    dataDiodeApp &app = dataDiodeApp::instance();

    if (argc == 1) {
        app.usage(argv[0]);
        return 1;
    }

    // open configuration files
    // configuration file names are hardcoded for security purposes
    std::FILE* inputFile = std::fopen("/etc/dataDiodeApp/sid.conf", "r");
    if (!inputFile) {
        std::cerr << "Unable to open configuration file. Exiting..." << std::endl;
        return -1;
    }
    uint32_t id, ret;
    ret = fscanf(inputFile, "%u", &id);
    uint16_t sId = static_cast<uint16_t>(id & 0xFF);
    std::fclose(inputFile);

    inputFile = std::fopen("/etc/dataDiodeApp/peerSid.conf", "r");
    if (!inputFile) {
        std::cerr << "Unable to open configuration file. Exiting..." << std::endl;
        return -1;
    }
    ret = fscanf(inputFile, "%u", &id);
    uint16_t peerSId = static_cast<uint16_t>(id & 0xFF);
    std::fclose(inputFile);

    inputFile = std::fopen("/etc/dataDiodeApp/peerMac.conf", "r");
    if (!inputFile) {
        std::cerr << "Unable to open configuration file. Exiting..." << std::endl;
        return -1;
    }

    uint32_t pM[6];
    ret = fscanf(inputFile, "%u:%u:%u:%u:%u:%u",
           &pM[0], &pM[1], &pM[2], &pM[3], &pM[4], &pM[5]);
    struct ether_addr peerMac;
    peerMac.addr_bytes[0] = static_cast<uint8_t>(pM[0] & 0xFF);
    peerMac.addr_bytes[1] = static_cast<uint8_t>(pM[1] & 0xFF);
    peerMac.addr_bytes[2] = static_cast<uint8_t>(pM[2] & 0xFF);
    peerMac.addr_bytes[3] = static_cast<uint8_t>(pM[3] & 0xFF);
    peerMac.addr_bytes[4] = static_cast<uint8_t>(pM[4] & 0xFF);
    peerMac.addr_bytes[5] = static_cast<uint8_t>(pM[5] & 0xFF);
    std::fclose(inputFile);

#ifdef _DD_TESTMODE_
    inputFile = std::fopen("/etc/dataDiodeApp/peerMac1.conf", "r");
    if (!inputFile) {
        std::cerr << "Unable to open configuration file. Exiting..." << std::endl;
        return -1;
    }

    pM[6];
    ret = fscanf(inputFile, "%u:%u:%u:%u:%u:%u",
           &pM[0], &pM[1], &pM[2], &pM[3], &pM[4], &pM[5]);
    struct ether_addr peerMac1;
    peerMac1.addr_bytes[0] = static_cast<uint8_t>(pM[0] & 0xFF);
    peerMac1.addr_bytes[1] = static_cast<uint8_t>(pM[1] & 0xFF);
    peerMac1.addr_bytes[2] = static_cast<uint8_t>(pM[2] & 0xFF);
    peerMac1.addr_bytes[3] = static_cast<uint8_t>(pM[3] & 0xFF);
    peerMac1.addr_bytes[4] = static_cast<uint8_t>(pM[4] & 0xFF);
    peerMac1.addr_bytes[5] = static_cast<uint8_t>(pM[5] & 0xFF);
    std::fclose(inputFile);
#endif

    // populate config parameters
#ifndef _DD_TESTMODE_
    app.configure(&peerMac, peerSId, sId);
#else
    app.configure(&peerMac, &peerMac1, peerSId, sId);
#endif

    // initialize application
    app.initialize(argc, argv);

    std::cout << "Exiting Data Diode Application! GoodBye" << std::endl;
    return 0;;
}

