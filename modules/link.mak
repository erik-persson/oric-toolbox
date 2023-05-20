#------------------------------------------------------------------------------
#
#  make-include for linking, installing and cleaning
#
#  Input variables:
#  MODULE_DIRS - list of directories to invoke 'make' and 'make clean' in
#  DOWNLOAD_FILES - files to be downloaded when missing
#  OBJS - list of object files for linker (based on SRCS)
#  PROG - name of program.
#  LIBS - additional dependencies for linking
#  LFLAGS - linker options
#  CLEAN_FILES - list of files to remove on 'make clean'
#  CLEAN_LIBS_DIRS - list of dirs to 'make clean' in on 'make clean-libs'
#  CONFIG_OUT_FILES - list of files to remove on 'make clean-config'
#
#  Copyright (c) 2005-2022 Erik Persson
#
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
# Recursive make rule
#------------------------------------------------------------------------------

.PHONY: modules

modules:
	@for X in $(MODULE_DIRS) ; do make -C $$X || exit 1 ; done

#------------------------------------------------------------------------------
# Download rule
#------------------------------------------------------------------------------

.PHONY: download

download: $(DOWNLOAD_FILES)

#------------------------------------------------------------------------------
# Config rule
#------------------------------------------------------------------------------

.PHONY: config

config: $(CONFIG_OUT_FILES)

#------------------------------------------------------------------------------
# Link rule
#------------------------------------------------------------------------------

$(PROG): $(OBJS) $(LIBS)
	@echo "Linking $@"
	@mkdir -p $(dir $@)
	@g++ $(CFLAGS) $(OBJS) -o $@ $(LIBS) $(LFLAGS)

#-----------------------------------------------------------------------------
# Test rule
#-----------------------------------------------------------------------------

# Placeholder
ifndef HAVE_TEST

.PHONY: test
test: all test-modules
ifdef MODULE_DIRS
    @:
else
	@echo No test defined for $(notdir $(shell pwd))
endif

endif

.PHONY: test-modules

test-modules:
	@for X in $(MODULE_DIRS) ; do make -C $$X test || exit 1 ; done

#------------------------------------------------------------------------------
# Install rule
#------------------------------------------------------------------------------

PREFIX ?= /usr/local

.PHONY: install

install:
	install -d $(PREFIX)/bin
	install -m 0755 $(PROG) $(PREFIX)/bin

#------------------------------------------------------------------------------
# Clean rules
#------------------------------------------------------------------------------

.PHONY: clean

clean:
	@rm -rf out $(CLEAN_FILES)
	@for X in $(MODULE_DIRS) ; do make -C $$X clean ; done
	@echo "Omitting libs/ directory from cleaning, use make clean-libs for that"

#------------------------------------------------------------------------------

.PHONY: clean-libs

clean-libs:
	@rm -rf out $(CLEAN_LIBS_FILES)
	@for X in $(CLEAN_LIBS_DIRS) ; do make -C $$X clean ; done

#------------------------------------------------------------------------------

.PHONY: clean-config

clean-config:
	@rm -rf out $(CONFIG_OUT_FILES)
