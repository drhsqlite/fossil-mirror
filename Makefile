#!/usr/bin/make
#
#### The directory in which Makefile fragments are stored.
#
MAKEDIR = ./make

#### The toplevel directory of the source tree.  Fossil can be built
#    in a directory that is separate from the source tree.  Just change
#    the following to point from the build directory to the src/ folder.
#
SRCDIR = ./src

#### Any site-specific pre-defined settings go here.  Settings in this file are
#    intended to direct the compilation below.
#
-include config.mk				# Configure if present.
ifndef CONFIG_MK_COMPLETE
  include $(MAKEDIR)/linux-gcc-config.mk	# Default to linux-gcc.
endif

#### The Tcl shell to run for test suites.
#
TCLSH = tclsh

# You should not need to change anything below this line
###############################################################################
include $(SRCDIR)/main.mk

