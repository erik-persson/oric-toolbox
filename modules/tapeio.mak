#==============================================================================
#
#  make-include for linking in the tapeio module in a program
#
#  Input variables:
#  TOP_DIR - parent directory of the modules/ directory
#
#  In/out variables:
#  LIBS        - adds library .a file here
#  MODULE_DIRS - adds directory where 'make' and 'make clean' will be invoked
#
#  Copyright (c) 2006-2022 Erik Persson
#
#==============================================================================

LIBS += $(TOP_DIR)/modules/tapeio/out/libtapeio.a
MODULE_DIRS += $(TOP_DIR)/modules/tapeio
