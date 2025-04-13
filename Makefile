#==============================================================================
#
#  Main makefile
#
#  Copyright (c) 2005-2022 Erik Persson
#
#==============================================================================

#
# Redirect all supported targets to the tools makefile
#
all download config test clean clean-libs clean-config install:
	@make -C tools $@

.PHONY: all download config test clean clean-libs clean-config install
