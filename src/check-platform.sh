#!/bin/sh

# This script checks that the default makefile is only used on platforms where it is
# positively known to work.
#
# Building on other platforms can result in subtly broken binaries.

HOST_OS=`uname -s`

# Check host OS, or whether this check has been disabled because we're running through
# the autosetup GNUmakefile.
if [ $HOST_OS == Linux ] || [ $HOST_OS == Darwin ] || [ X$1 == Xdisable ]
then
    touch bld/checked-platform.$1
else
    echo
    echo The default Makefile cannot be used on this platform.
    echo
    echo Use
    echo
    echo "   ./configure; make -f GNUmakefile"
    echo
    echo to build fossil.
    echo
    exit 1
fi
