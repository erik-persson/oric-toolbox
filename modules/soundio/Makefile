#------------------------------------------------------------------------------
#
#  Makefile for the 'soundio' module
#
#  Generate libsoundio.a
#
#  Copyright (c) 2006 Erik Persson
#
#------------------------------------------------------------------------------

TOP_DIR=../..

.PHONY: all
all: out/libsoundio.a
	@:

SRCS += SoundReader.cpp
SRCS += SoundWriter.cpp
SRCS += SoundPlayer.cpp
SRCS += SoundRecorder.cpp
SRCS += SoundPort.cpp
SRCS += SoundSink.cpp
SRCS += Sound.cpp
SRCS += Downsampler.cpp

CFLAGS = -Wall -Wextra -Werror
CFLAGS += -Wreturn-type
CFLAGS += -g -O2 -I/usr/local/include -I..

#------------------------------------------------------------------------------
# Imported libraries
#------------------------------------------------------------------------------

include $(TOP_DIR)/libs/libsndfile.mak

# Optional
-include $(TOP_DIR)/libs/mpg123.mak

include $(TOP_DIR)/libs/portaudio.mak

#------------------------------------------------------------------------------
# Compile
#------------------------------------------------------------------------------

include $(TOP_DIR)/modules/compile.mak
include $(TOP_DIR)/modules/link.mak

#------------------------------------------------------------------------------
# Link
#------------------------------------------------------------------------------

out/libsoundio.a: $(OBJS) Makefile
	@echo "Creating $@"
	@ar ru out/libsoundio.a $(OBJS)
	@ranlib out/libsoundio.a
