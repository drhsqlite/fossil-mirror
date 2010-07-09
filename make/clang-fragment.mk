#### C Compiler and options for use in building executables that
#    will run on the platform that is doing the build.  This is used
#    to compile code-generator programs as part of the build process.
#    See TCC below for the C compiler for building the finished binary.
#
BCC = clang -g -O2

#### C Compile and options for use in building executables that
#    will run on the target platform.  This is usually the same
#    as BCC, unless you are cross-compiling.  This C compiler builds
#    the finished binary for fossil.  The BCC compiler above is used
#    for building intermediate code-generator tools.
#
TCC = clang -g -Os -Wall

#### Compiler options.
#    The variables tested are defined in the make/PLATFORM-fragment.mk files.
#
ifdef ENABLE_SSL
  TCC += -DFOSSIL_ENABLE_SSL=1
endif
ifndef ENABLE_I18N
  TCC += -DFOSSIL_I18N=0
endif
ifdef PLATFORM_SPECIFIC_CLANG
  TCC += $(PLATFORM_SPECIFIC_CLANG)
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
ifdef PLATFORM_SPECIFIC_LIB
  TCC += $(PLATFORM_SPECIFIC_LIB)
endif

