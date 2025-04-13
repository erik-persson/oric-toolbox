#==============================================================================
#
#  make-include for linking in the soundio module in a program
#
#  Input variables:
#  TOP_DIR - parent directory of the modules/ directory
#
#  In/out variables:
#  LIBS        - adds library .a file here
#  CFLAGS      - adds flags here
#  MODULE_DIRS - adds directory where 'make' and 'make clean' will be invoked
#
#  Copyright (c) 2006-2022 Erik Persson
#
#==============================================================================

LIBS += $(TOP_DIR)/modules/soundio/out/libsoundio.a
MODULE_DIRS += $(TOP_DIR)/modules/soundio

#------------------------------------------------------------------------------
# Inclusion of dependencies
#------------------------------------------------------------------------------

include $(TOP_DIR)/libs/libsndfile.mak
include $(TOP_DIR)/libs/portaudio.mak

# Optional dependencies
-include $(TOP_DIR)/libs/mpg123.mak
