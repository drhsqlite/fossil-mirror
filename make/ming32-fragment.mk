#### The suffix to add to executable files.
#
E = .exe

#### The directory into which object code files should be written.
#
OBJDIR = ./wobj

#### The following variable definitions decide which features are turned on or
#    of when building Fossil.  Comment out the features which are not needed by
#    this platform.
#
ENABLE_STATIC = 1	# we want a static build

#### The following features must be added to the GCC and LD builds respectively.
#
ifndef MING32_GCC
PLATFORM_SPECIFIC_GCC = -L/mingw/lib -I/mingw/include
else
PLATFORM_SPECIFIC_GCC = $(MING32_GCC)
endif

ifndef MING32_LIB
PLATFORM_SPECIFIC_LIB = -lmingwex -lws2_32
else
PLATFORM_SPECIFIC_LIB = $(MING32_LIB)
endif

