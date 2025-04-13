#==============================================================================
#
#  make-include which does the following:
#  Generate compile rules with dependencies from the variable SRCS
#  Object files are placed under out/*.o
#
#  Input variables:
#  SRCS   - list of C and C++ source files
#  CFLAGS - list of C compiler options
#  CPPFLAGS (optional) list of C++ compiler options (defaults to CFLAGS)
#  PREDEPS - optional dependencies before depend pass
#
#  Output variables:
#  OBJS - list of object files for linker (based on SRCS)
#
#  Copyright (c) 2005-2022 Erik Persson
#
#==============================================================================

C_SRCS= $(filter %.c,$(SRCS))
CPP_SRCS= $(filter %.cpp,$(SRCS))

OBJS = $(patsubst %.c,out/%.o,$(patsubst %.cpp,out/%.o,$(notdir $(SRCS))))

CC=gcc

ifndef CPPFLAGS
    CPPFLAGS := $(CFLAGS)
endif

SYS := $(shell $(CC) -dumpmachine)

ifneq (, $(findstring cygwin, $(SYS)))
  # In cygwin: allow extensions so we can use Posix functions
  CPPFLAGS += -std=gnu++17
else
  # Use C++17 for all C++ files
  CPPFLAGS += -std=c++17
endif

# GCC 7 gives impractical warnings about signed overflow.
# Suppress centrally since it is likely affecting lots of code.
# The problem is said to be fixed in GCC 8.
CFLAGS += -Wno-strict-overflow
CPPFLAGS += -Wno-strict-overflow

.PHONY: depend
depend out/depend.mak: Makefile $(PREDEPS)
	@echo "Checking dependencies"
	@mkdir -p out
	@printf "" >out/depend.mak
	@OK="1" ; for X in $(CPP_SRCS) ; do \
		printf "out/" >> out/depend.mak ; \
		$(CC) -MM $(CPPFLAGS) $(CFLAGS_PCH) $$X | grep -v "#" >> out/depend.mak || OK="" ; \
		printf "\t@echo $$<\n" >> out/depend.mak ; \
		printf "\t@$(CC) $(CPPFLAGS) $(CFLAGS_PCH) -c -o \$$@ $$<\n" >> out/depend.mak ; \
		test -z $$OK && rm out/depend.mak ; \
		test -z $$OK && exit 1 ; \
	done ; \
	OK="1" ; for X in $(C_SRCS) ; do \
		printf "out/" >> out/depend.mak ; \
		$(CC) -MM $(CFLAGS) $$X | grep -v "#" >> out/depend.mak || OK="" ; \
		printf "\t@echo $$<\n" >> out/depend.mak ; \
		printf "\t@$(CC) $(CFLAGS) -c -o \$$@ $$<\n" >> out/depend.mak ; \
		test -z $$OK && rm out/depend.mak ; \
		test -z $$OK && exit 1 ; \
	done ; \
	echo Dependencies generated OK

# Do not include the dependency file upon targets that are not meant to compile
ifeq (,$(filter $(MAKECMDGOALS),download config clean clean-libs clean-config))
    include out/depend.mak
endif
