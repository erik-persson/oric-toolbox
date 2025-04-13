#==============================================================================
#
#  Makefile for the 'tapeio' module
#
#  Generate libtapeio.a
#
#  Copyright (c) 2006 Erik Persson
#
#==============================================================================

TOP_DIR=../..
PROG=out/test
HAVE_TEST=1

.PHONY: all
all: modules out/libtapeio.a $(PROG)
	@:

SRCS += filters.cpp
SRCS += Demodulator.cpp
SRCS += Balancer.cpp
SRCS += TrivialDecoder.cpp
SRCS += DemodDecoder.cpp
SRCS += DualDecoder.cpp
SRCS += LowpassFilter.cpp
SRCS += PatternBinarizer.cpp
SRCS += GridBinarizer.cpp
SRCS += SuperBinarizer.cpp
SRCS += XenonDecoder.cpp
SRCS += TapeDecoder.cpp
SRCS += TapeEncoder.cpp
SRCS += TapeParser.cpp
SRCS += test.cpp

CFLAGS = -Wall -Wextra -Werror
CFLAGS += -Wreturn-type
CFLAGS += -g -O2 -I/usr/local/include -I..

#------------------------------------------------------------------------------
# Imported modules
#------------------------------------------------------------------------------

include $(TOP_DIR)/modules/soundio.mak

#------------------------------------------------------------------------------
# Compile
#------------------------------------------------------------------------------

include $(TOP_DIR)/modules/compile.mak
include $(TOP_DIR)/modules/link.mak

#------------------------------------------------------------------------------
# Link
#------------------------------------------------------------------------------

out/libtapeio.a: $(OBJS) Makefile
	@echo "Creating $@"
	@ar ru out/libtapeio.a $(filter-out out/test.o,$(OBJS))
	@ranlib out/libtapeio.a

#-----------------------------------------------------------------------------
# Test rule
#-----------------------------------------------------------------------------

.PHONY: test
test: all test-modules
	$(PROG) -t
