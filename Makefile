#!/usr/bin/make
#
#### The directory in which Makefile fragments are stored.
#
MAKEDIR = ./make

#### Set up our compiler if it hasn't already been defined.

ifndef COMPILER
  COMPILER = gcc
endif

#### Set up our platform if it hasn't already been defined.
#
ifndef PLATFORM
  # We default to Linux.
  # TODO: Figure out how to reliably identify the platform from Make.  Sadly the
  #       OSTYPE environment variable isn't carried through into GNU Make, so we
  #       can't do this the obvious way.
  PLATFORM = linux
endif

#### The toplevel directory of the source tree.  Fossil can be built
#    in a directory that is separate from the source tree.  Just change
#    the following to point from the build directory to the src/ folder.
#
SRCDIR = ./src

#### Include the fragments we need from our specific environment.
#
include $(MAKEDIR)/$(PLATFORM)-fragment.mk
include $(MAKEDIR)/$(COMPILER)-fragment.mk

#### Include a locale-specific configuration make fragment if present.
#    Any modification to the platforms' generic setups should be made in this
#    file where possible.
-include config.mk

#### The following section beginning after #+++ and ending before #--- is used
#    inside the $(PLATFORM)-fragment.mk files to turn on the features required
#    or desired by builds on that platform.  They are replicated here for
#    documentation purposes only and should not be set in this file.
#+++
#### The following variable definitions decide which features are turned on or
#    of when building Fossil.  Comment out the features which are not needed by
#    this platform.
#
#ENABLE_STATIC = 1	# we want a static build
#ENABLE_SSL = 1		# we are using SSL
#ENABLE_SOCKET = 1	# we are using libsocket (OpenSolaris and Solaris)
#ENABLE_NSL = 1		# we are using libnsl library (Solaris)
#ENABLE_I18N = 1	# we are using i18n settings
#---

#### The Tcl shell to run for test suites.
#
TCLSH = tclsh

# You should not need to change anything below this line
###############################################################################
include $(SRCDIR)/main.mk

