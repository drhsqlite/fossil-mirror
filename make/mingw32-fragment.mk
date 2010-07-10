#### OS-specific configuration for building Fossil on MingGW32 systems.
#

#### The suffix to add to executable files.
#
E = .exe

#### The directory into which object code files should be written.
#
OBJDIR = ./wobj

#### MinGW32 can only support the GCC compiler.  Force this.
#
COMPILER = gcc

#### The following variable definitions decide which features are turned on or
#    of when building Fossil.  Comment out the features which are not needed by
#    this platform.
#
ENABLE_STATIC = 1	# we want a static build
#ENABLE_SSL = 1		# we are using SSL
#ENABLE_SOCKET = 1	# we are using libsocket (OpenSolaris and Solaris)
#ENABLE_NSL = 1		# we are using libnsl library (Solaris)
#ENABLE_I18N = 1	# we are using i18n settings

#### The following features must be added to the GCC and LD builds respectively.
#
ifndef MINGW32_GCC
PLATFORM_SPECIFIC_GCC = -L/mingw/lib -I/mingw/include
else
PLATFORM_SPECIFIC_GCC = $(MINGW32_GCC)
endif

ifndef MINGW32_LIB
PLATFORM_SPECIFIC_LIB = -lmingwex -lws2_32
else
PLATFORM_SPECIFIC_LIB = $(MINGW32_LIB)
endif

