#----------------------------------------------------------------------------
#
#  basictool makefile
#
#  Copyright (c) 2021-2022 Erik Persson
#
#-----------------------------------------------------------------------------

TOP_DIR=../..
PROG=$(TOP_DIR)/bin/basictool

SRCS += basictool.cpp

.PHONY: all
all: modules $(PROG)
	@:

#----------------------------------------------------------------------------

CFLAGS= -Wall -Wextra -Werror
CFLAGS += -g -O2

CFLAGS += -I$(TOP_DIR)/modules

#------------------------------------------------------------------------------
# Imported modules
#------------------------------------------------------------------------------

include $(TOP_DIR)/modules/tapeio.mak
include $(TOP_DIR)/modules/soundio.mak

#-----------------------------------------------------------------------------
# Compile, link and clean rules
#-----------------------------------------------------------------------------

include $(TOP_DIR)/modules/compile.mak
include $(TOP_DIR)/modules/link.mak
