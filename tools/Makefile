#==============================================================================
#
#  Makefile for 'tools' directory
#
#  Copyright (c) 2005-2022 Erik Persson
#
#==============================================================================

.PHONY: all download config test install clean clean-libs clean-config

TOOLS += basictool
TOOLS += taperescue

all download config test install:
	@for X in $(TOOLS) ; do make -C $$X $@ || exit 1 ; done

clean:
	@for X in $(TOOLS) ; do make -C $$X $@ ; done
	@rm -rf ../bin

clean-libs clean-config:
	@for X in $(TOOLS) ; do make -C $$X $@ ; done
