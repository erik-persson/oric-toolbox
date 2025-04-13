#==============================================================================
#
#  Makefile for the 'option' module
#
#  Generate liboption.a
#
#  Copyright (c) 2006 Erik Persson
#
#==============================================================================

TOP_DIR=../..

.PHONY: all
all: out/liboption.a
	@:

SRCS= Option.cpp

CFLAGS = -Wall -Wextra -Werror
CFLAGS += -Wreturn-type
CFLAGS += -g -O2 -I/usr/local/include -I..

#------------------------------------------------------------------------------
# Compile
#------------------------------------------------------------------------------

include $(TOP_DIR)/modules/compile.mak
include $(TOP_DIR)/modules/link.mak

#------------------------------------------------------------------------------
# Link
#------------------------------------------------------------------------------

out/liboption.a: $(OBJS) Makefile
	@echo "Creating $@"
	@ar ru out/liboption.a $(OBJS)
	@ranlib out/liboption.a
