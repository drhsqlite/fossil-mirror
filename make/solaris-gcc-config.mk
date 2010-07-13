#### config.mk file for Solaris with GCC.
#    Copy this file as config.mk in the Fossil root directory to use.
#

#### OS-specific configuration for building Fossil on Solaris systems.
#

#### The suffix to add to executable files.  ".exe" for windows.
#    Nothing for unix.
#
E =

#### The directory into which object code files should be written.
#
OBJDIR = ./obj

#### The following variable definitions decide which features are turned on or
#    of when building Fossil.  Comment out the features which are not needed by
#    this platform.
#
#ENABLE_STATIC = 1	# we want a static build
ENABLE_SSL = 1		# we are using SSL
ENABLE_SOCKET = 1	# we are using libsocket (OpenSolaris and Solaris)
ENABLE_NSL = 1		# we are using libnsl library (Solaris)
ENABLE_I18N = 1		# we are using i18n settings

#### Compiler-specific configuration for users of the GCC compiler suite.
#

#### C Compiler and options for use in building executables that
#    will run on the platform that is doing the build.  This is used
#    to compile code-generator programs as part of the build process.
#    See TCC below for the C compiler for building the finished binary.
#
BCC = gcc -g -O2

#### C Compile and options for use in building executables that
#    will run on the target platform.  This is usually the same
#    as BCC, unless you are cross-compiling.  This C compiler builds
#    the finished binary for fossil.  The BCC compiler above is used
#    for building intermediate code-generator tools.
#
TCC = gcc -g -Os -Wall

#### Compiler options.
#    The variables tested are defined in the make/PLATFORM-fragment.mk files.
#
ifdef ENABLE_SSL
  TCC += -DFOSSIL_ENABLE_SSL=1
endif
ifndef ENABLE_I18N
  TCC += -DFOSSIL_I18N=0
endif

#### Linker dependencies.  Fossil only requires libz as an external dependency.
#    All other library settings are optional and toggled in platform-specific
#    make fragments.
#
LIB = -lz $(LDFLAGS)

#### Linker options.
#    The variables tested are defined in the make/PLATFORM-fragment.mk files.
#
ifdef ENABLE_STATIC
  LIB += -static
endif
ifdef ENABLE_SSL
  LIB += -lcrypto -lssl
endif
ifdef ENABLE_SOCKET
  LIB += -lsocket
endif
ifdef ENABLE_NSL
  LIB += -lnsl
endif

#### Signal that we've used a config.mk file.
#
CONFIG_MK_COMPLETE=1

