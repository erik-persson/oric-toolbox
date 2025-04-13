#==============================================================================
#
#  Make-include for downloading, building and using libsndfile
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

LIBSNDFILE = $(TOP_DIR)/libs/libsndfile
PREDEPS += $(LIBSNDFILE)/include/sndfile.h
LIBS += $(LIBSNDFILE)/src/.libs/libsndfile.a
CFLAGS += -I$(LIBSNDFILE)/include
CLEAN_LIBS_DIRS += $(LIBSNDFILE)
CLEAN_LIBS_FILES += $(LIBSNDFILE)/src/.libs/libsndfile.a
CONFIG_OUT_FILES += $(LIBSNDFILE)/Makefile

#------------------------------------------------------------------------------
# Download step
#------------------------------------------------------------------------------

LIBSNDFILE_VERSION=1.1.0
LIBSNDFILE_TGZ=$(TOP_DIR)/libs/libsndfile.tar.gz
DOWNLOAD_FILES += $(LIBSNDFILE)/configure.ac

$(LIBSNDFILE)/configure.ac:
	@echo "Downloading libsndfile"
	wget https://github.com/libsndfile/libsndfile/archive/refs/tags/$(LIBSNDFILE_VERSION).tar.gz -O $(LIBSNDFILE_TGZ) &&\
	tar -xf $(LIBSNDFILE_TGZ) -C $(TOP_DIR)/libs &&\
	mv $(TOP_DIR)/libs/libsndfile-$(LIBSNDFILE_VERSION) $(LIBSNDFILE) &&\
	rm -f $(LIBSNDFILE_TGZ) ||\
	( echo "libsndfile download failed" >&2 ; exit 1 )

#------------------------------------------------------------------------------
# Autogen step
#------------------------------------------------------------------------------

$(LIBSNDFILE)/configure: $(LIBSNDFILE)/configure.ac
	(cd $(LIBSNDFILE) ; autoreconf -vif)

#------------------------------------------------------------------------------
# Configure step
#------------------------------------------------------------------------------

# We disable external libs, this means no ogg, vorbis or FLAC
$(LIBSNDFILE)/Makefile: $(LIBSNDFILE)/configure
	(cd $(LIBSNDFILE) ; ./configure --enable-static --disable-shared --disable-external-libs)

#------------------------------------------------------------------------------
# Main build step
#------------------------------------------------------------------------------

$(LIBSNDFILE)/src/.libs/libsndfile.a $(LIBSNDFILE)/include/sndfile.h: $(LIBSNDFILE)/Makefile
	make -C $(LIBSNDFILE)
