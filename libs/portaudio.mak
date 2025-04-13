#==============================================================================
#
#  Make-include for downloading, building and using PortAudio
#
#  In/out variables:
#  LIBS   - adds library .a files here
#  CFLAGS - adds compiler flags here
#  LFLAGS - adds linker flags here
#  PREDEPS - adds header files here
#  DOWNLOAD_FILES - files that trigger download when missing
#  CLEAN_LIBS_DIRS - directories to 'make clean' in on 'make clean-libs'
#  CLEAN_LIBS_FILES - files to remove on 'make clean-libs'
#  CONFIG_OUT_FILES - files to remove to trigger new configure
#
#  Copyright (c) 2006-2022 Erik Persson
#
#==============================================================================

PORTAUDIO = $(TOP_DIR)/libs/portaudio
PREDEPS += $(PORTAUDIO)/include/portaudio.h
LIBS += $(PORTAUDIO)/lib/.libs/libportaudio.a
CFLAGS += -I$(PORTAUDIO)/include
CLEAN_LIBS_DIRS += $(PORTAUDIO)
CLEAN_LIBS_FILES += $(PORTAUDIO)/lib/.libs/libportaudio.a
CONFIG_OUT_FILES += $(PORTAUDIO)/Makefile

ifeq ($(OSTYPE),msys)
   # mingw
   UNAME := Windows
else
   UNAME ?= $(shell uname)
   ifneq (, $(findstring CYGWIN, $(UNAME)))
       UNAME := Windows
   endif
endif

# Dependencies in Linux
ifeq ($(UNAME),Linux)
    LFLAGS += -lrt
    LFLAGS += -lasound
    LFLAGS += -lpthread
endif

# Dependencies in Windows
ifeq ($(UNAME),Windows)
    LFLAGS += -lwinmm
endif

# Dependencies in macos
ifeq ($(UNAME),Darwin)
    LFLAGS += -framework CoreServices
    LFLAGS += -framework CoreFoundation
    LFLAGS += -framework AudioUnit
    LFLAGS += -framework AudioToolbox
    LFLAGS += -framework CoreAudio
endif

#------------------------------------------------------------------------------
# Download step
#------------------------------------------------------------------------------

PORTAUDIO_VERSION=19.7.0
PORTAUDIO_TGZ=$(TOP_DIR)/libs/portaudio.tar.gz
DOWNLOAD_FILES += $(PORTAUDIO)/configure

$(PORTAUDIO)/include/portaudio.h $(PORTAUDIO)/configure:
	@echo "Downloading PortAudio"
	wget https://github.com/PortAudio/portaudio/archive/refs/tags/v$(PORTAUDIO_VERSION).tar.gz -O $(PORTAUDIO_TGZ) &&\
	tar -xf $(PORTAUDIO_TGZ) -C $(TOP_DIR)/libs &&\
	mv $(TOP_DIR)/libs/portaudio-$(PORTAUDIO_VERSION) $(PORTAUDIO) &&\
	rm -f $(PORTAUDIO_TGZ) ||\
	( echo "PortAudio download failed" >&2 ; exit 1 )

#------------------------------------------------------------------------------
# Configure step
#------------------------------------------------------------------------------

# On cygwin we get no static library unless we specify --disable-shared
$(PORTAUDIO)/Makefile: $(PORTAUDIO)/configure
	(cd $(PORTAUDIO) ; ./configure --enable-static --disable-shared)

#------------------------------------------------------------------------------
# Main build step
#------------------------------------------------------------------------------

$(PORTAUDIO)/lib/.libs/libportaudio.a: $(PORTAUDIO)/Makefile
	make -C $(PORTAUDIO)
