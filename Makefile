#
# Copyright (C) 2020 Pankaj Malviya
#
# This file is part of the data diode application "IN4004"

# This is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>
#

# binary name
APP = datadiode

# all source are stored in SRCS-y
SRCS-y += src/dataDiode.cpp src/ddPort.cpp src/main.cpp

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

INCLUDES := -I../include/
ifneq ($(_DD_TESTMODE_),)
$(warning "Building the application in TEST mode. DO NOT use the binary in production")
CXXFLAGS += -D _DD_TESTMODE_
endif
CXXFLAGS += -O3 $(INCLUDES)
#CXXFLAGS += $(WERROR_FLAGS)
LDFLAGS += -lstdc++

include $(RTE_SDK)/mk/rte.extapp.mk

