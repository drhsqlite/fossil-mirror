#!/usr/bin/tclsh
#
# Run this Tcl script to generate the various makefiles for a variety
# of platforms.  Files generated include:
#
#     src/main.mk           # makefile for all unix systems
#     win/Makefile.mingw    # makefile for mingw on windows
#     win/Makefile.*        # makefiles for other windows compilers
#
# Run this script while in the "src" subdirectory.  Like this:
#
#      tclsh makemake.tcl
#
#############################################################################

# Basenames of all source files that get preprocessed using
# "translate" and "makeheaders".  To add new C-language source files to the
# project, simply add the basename to this list and rerun this script.
#
# Set the separate extra_files variable further down for how to add non-C
# files, such as string and BLOB resources.
#
set src {
  add
  allrepo
  attach
  bag
  bisect
  blob
  branch
  browse
  builtin
  bundle
  cache
  captcha
  cgi
  checkin
  checkout
  clearsign
  clone
  comformat
  configure
  content
  db
  delta
  deltacmd
  descendants
  diff
  diffcmd
  dispatch
  doc
  encode
  event
  export
  file
  finfo
  foci
  fshell
  fusefs
  glob
  graph
  gzip
  hname
  http
  http_socket
  http_transport
  import
  info
  json
  json_artifact
  json_branch
  json_config
  json_diff
  json_dir
  json_finfo
  json_login
  json_query
  json_report
  json_status
  json_tag
  json_timeline
  json_user
  json_wiki
  leaf
  loadctrl
  login
  lookslike
  main
  manifest
  markdown
  markdown_html
  md5
  merge
  merge3
  moderate
  name
  path
  piechart
  pivot
  popen
  pqueue
  printf
  publish
  purge
  rebuild
  regexp
  report
  rss
  schema
  search
  security_audit
  setup
  sha1
  sha1hard
  sha3
  shun
  sitemap
  skins
  sqlcmd
  stash
  stat
  statrep
  style
  sync
  tag
  tar
  th_main
  timeline
  tkt
  tktsetup
  undo
  unicode
  unversioned
  update
  url
  user
  utf8
  util
  verify
  vfile
  wiki
  wikiformat
  winfile
  winhttp
  wysiwyg
  xfer
  xfersetup
  zip
  http_ssl
}

# Additional resource files that get built into the executable.
#
set extra_files {
  diff.tcl
  markdown.md
  ../skins/*/*.txt
}

# Options used to compile the included SQLite library.
#
set SQLITE_OPTIONS {
  -DNDEBUG=1
  -DSQLITE_THREADSAFE=0
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
  -DSQLITE_OMIT_DECLTYPE
  -DSQLITE_OMIT_DEPRECATED
  -DSQLITE_OMIT_GET_TABLE
  -DSQLITE_OMIT_PROGRESS_CALLBACK
  -DSQLITE_OMIT_SHARED_CACHE
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_MAX_EXPR_DEPTH=0
  -DSQLITE_USE_ALLOCA
  -DSQLITE_ENABLE_LOCKING_STYLE=0
  -DSQLITE_DEFAULT_FILE_FORMAT=4
  -DSQLITE_ENABLE_EXPLAIN_COMMENTS
  -DSQLITE_ENABLE_FTS4
  -DSQLITE_ENABLE_FTS3_PARENTHESIS
  -DSQLITE_ENABLE_DBSTAT_VTAB
  -DSQLITE_ENABLE_JSON1
  -DSQLITE_ENABLE_FTS5
  -DSQLITE_ENABLE_STMTVTAB
}
#lappend SQLITE_OPTIONS -DSQLITE_ENABLE_FTS3=1
#lappend SQLITE_OPTIONS -DSQLITE_ENABLE_STAT4
#lappend SQLITE_OPTIONS -DSQLITE_WIN32_NO_ANSI
#lappend SQLITE_OPTIONS -DSQLITE_WINNT_MAX_PATH_CHARS=4096

# Options used to compile the included SQLite shell.
#
set SHELL_OPTIONS {
  -Dmain=sqlite3_shell
  -DSQLITE_SHELL_IS_UTF8=1
  -DSQLITE_OMIT_LOAD_EXTENSION=1
  -DUSE_SYSTEM_SQLITE=$(USE_SYSTEM_SQLITE)
  -DSQLITE_SHELL_DBNAME_PROC=fossil_open
}

# miniz (libz drop-in alternative) precompiler flags.
#
set MINIZ_OPTIONS {
  -DMINIZ_NO_STDIO
  -DMINIZ_NO_TIME
  -DMINIZ_NO_ARCHIVE_APIS
}

# Options used to compile the included SQLite shell on Windows.
#
set SHELL_WIN32_OPTIONS $SHELL_OPTIONS
lappend SHELL_WIN32_OPTIONS -Daccess=file_access
lappend SHELL_WIN32_OPTIONS -Dsystem=fossil_system
lappend SHELL_WIN32_OPTIONS -Dgetenv=fossil_getenv
lappend SHELL_WIN32_OPTIONS -Dfopen=fossil_fopen

# Name of the final application
#
set name fossil

# The "writeln" command sends output to the target makefile.
#
proc writeln {args} {
  global output_file
  if {[lindex $args 0]=="-nonewline"} {
    puts -nonewline $output_file [lindex $args 1]
  } else {
    puts $output_file [lindex $args 0]
  }
}

# STOP HERE.
# Unless the build procedures changes, you should not have to edit anything
# below this line.

# Expand any wildcards in "extra_files"
set new_extra_files {}
foreach file $extra_files {
  foreach x [glob -nocomplain $file] {
    lappend new_extra_files $x
  }
}
set extra_files $new_extra_files

##############################################################################
##############################################################################
##############################################################################
# Start by generating the "main.mk" makefile used for all unix systems.
#
puts "building main.mk"
set output_file [open main.mk w]
fconfigure $output_file -translation binary

writeln {#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "src/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
# This file is included by primary Makefile.
#

XBCC = $(BCC) $(BCCFLAGS) $(CFLAGS)
XTCC = $(TCC) -I. -I$(SRCDIR) -I$(OBJDIR) $(TCCFLAGS) $(CFLAGS)

}
writeln -nonewline "SRC ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n  \$(SRCDIR)/$s.c"
}
writeln "\n"
writeln -nonewline "EXTRA_FILES ="
foreach s [lsort $extra_files] {
  writeln -nonewline " \\\n  \$(SRCDIR)/$s"
}
writeln "\n"
writeln -nonewline "TRANS_SRC ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n  \$(OBJDIR)/${s}_.c"
}
writeln "\n"
writeln -nonewline "OBJ ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n \$(OBJDIR)/$s.o"
}
writeln "\n"
writeln "APPNAME = $name\$(E)"
writeln "\n"

writeln [string map [list \
    <<<SQLITE_OPTIONS>>> [join $SQLITE_OPTIONS " \\\n                 "] \
    <<<SHELL_OPTIONS>>> [join $SHELL_OPTIONS " \\\n                "] \
    <<<MINIZ_OPTIONS>>> [join $MINIZ_OPTIONS " \\\n                "]] {
all:	$(OBJDIR) $(APPNAME)

install:	$(APPNAME)
	mkdir -p $(INSTALLDIR)
	mv $(APPNAME) $(INSTALLDIR)

codecheck:	$(TRANS_SRC) $(OBJDIR)/codecheck1
	$(OBJDIR)/codecheck1 $(TRANS_SRC)

$(OBJDIR):
	-mkdir $(OBJDIR)

$(OBJDIR)/translate:	$(SRCDIR)/translate.c
	$(XBCC) -o $(OBJDIR)/translate $(SRCDIR)/translate.c

$(OBJDIR)/makeheaders:	$(SRCDIR)/makeheaders.c
	$(XBCC) -o $(OBJDIR)/makeheaders $(SRCDIR)/makeheaders.c

$(OBJDIR)/mkindex:	$(SRCDIR)/mkindex.c
	$(XBCC) -o $(OBJDIR)/mkindex $(SRCDIR)/mkindex.c

$(OBJDIR)/mkbuiltin:	$(SRCDIR)/mkbuiltin.c
	$(XBCC) -o $(OBJDIR)/mkbuiltin $(SRCDIR)/mkbuiltin.c

$(OBJDIR)/mkversion:	$(SRCDIR)/mkversion.c
	$(XBCC) -o $(OBJDIR)/mkversion $(SRCDIR)/mkversion.c

$(OBJDIR)/codecheck1:	$(SRCDIR)/codecheck1.c
	$(XBCC) -o $(OBJDIR)/codecheck1 $(SRCDIR)/codecheck1.c

# Run the test suite.
# Other flags that can be included in TESTFLAGS are:
#
#  -halt     Stop testing after the first failed test
#  -keep     Keep the temporary workspace for debugging
#  -prot     Write a detailed log of the tests to the file ./prot
#  -verbose  Include even more details in the output
#  -quiet    Hide most output from the terminal
#  -strict   Treat known bugs as failures
#
# TESTFLAGS can also include names of specific test files to limit
# the run to just those test cases.
#
test:	$(OBJDIR) $(APPNAME)
	$(TCLSH) $(SRCDIR)/../test/tester.tcl $(APPNAME) -quiet $(TESTFLAGS)

$(OBJDIR)/VERSION.h:	$(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(SRCDIR)/../VERSION $(OBJDIR)/mkversion
	$(OBJDIR)/mkversion $(SRCDIR)/../manifest.uuid \
		$(SRCDIR)/../manifest \
		$(SRCDIR)/../VERSION >$(OBJDIR)/VERSION.h

# Setup the options used to compile the included SQLite library.
SQLITE_OPTIONS = <<<SQLITE_OPTIONS>>>

# Setup the options used to compile the included SQLite shell.
SHELL_OPTIONS = <<<SHELL_OPTIONS>>>

# Setup the options used to compile the included miniz library.
MINIZ_OPTIONS = <<<MINIZ_OPTIONS>>>

# The USE_SYSTEM_SQLITE variable may be undefined, set to 0, or set
# to 1. If it is set to 1, then there is no need to build or link
# the sqlite3.o object. Instead, the system SQLite will be linked
# using -lsqlite3.
SQLITE3_OBJ.0 = $(OBJDIR)/sqlite3.o
SQLITE3_OBJ.1 =
SQLITE3_OBJ.  = $(SQLITE3_OBJ.0)

# The FOSSIL_ENABLE_MINIZ variable may be undefined, set to 0, or
# set to 1.  If it is set to 1, the miniz library included in the
# source tree should be used; otherwise, it should not.
MINIZ_OBJ.0 =
MINIZ_OBJ.1 = $(OBJDIR)/miniz.o
MINIZ_OBJ.  = $(MINIZ_OBJ.0)

# The USE_LINENOISE variable may be undefined, set to 0, or set
# to 1. If it is set to 0, then there is no need to build or link
# the linenoise.o object.
LINENOISE_DEF.0 =
LINENOISE_DEF.1 = -DHAVE_LINENOISE
LINENOISE_DEF.  = $(LINENOISE_DEF.0)
LINENOISE_OBJ.0 =
LINENOISE_OBJ.1 = $(OBJDIR)/linenoise.o
LINENOISE_OBJ.  = $(LINENOISE_OBJ.0)

# The USE_SEE variable may be undefined, 0 or 1.  If undefined or
# 0, ordinary SQLite is used.  If 1, then sqlite3-see.c (not part of
# the source tree) is used and extra flags are provided to enable
# the SQLite Encryption Extension.
SQLITE3_SRC.0 = sqlite3.c
SQLITE3_SRC.1 = sqlite3-see.c
SQLITE3_SRC. = sqlite3.c
SQLITE3_SRC = $(SRCDIR)/$(SQLITE3_SRC.$(USE_SEE))
SQLITE3_SHELL_SRC.0 = shell.c
SQLITE3_SHELL_SRC.1 = shell-see.c
SQLITE3_SHELL_SRC. = shell.c
SQLITE3_SHELL_SRC = $(SRCDIR)/$(SQLITE3_SHELL_SRC.$(USE_SEE))
SEE_FLAGS.0 =
SEE_FLAGS.1 = -DSQLITE_HAS_CODEC -DSQLITE_SHELL_DBKEY_PROC=fossil_key
SEE_FLAGS. =
SEE_FLAGS = $(SEE_FLAGS.$(USE_SEE))
}]

writeln [string map [list <<<NEXT_LINE>>> \\] {
EXTRAOBJ = <<<NEXT_LINE>>>
 $(SQLITE3_OBJ.$(USE_SYSTEM_SQLITE)) <<<NEXT_LINE>>>
 $(MINIZ_OBJ.$(FOSSIL_ENABLE_MINIZ)) <<<NEXT_LINE>>>
 $(LINENOISE_OBJ.$(USE_LINENOISE)) <<<NEXT_LINE>>>
 $(OBJDIR)/shell.o <<<NEXT_LINE>>>
 $(OBJDIR)/th.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_lang.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_tcl.o <<<NEXT_LINE>>>
 $(OBJDIR)/cson_amalgamation.o
}]

writeln {
$(APPNAME):	$(OBJDIR)/headers $(OBJDIR)/codecheck1 $(OBJ) $(EXTRAOBJ)
	$(OBJDIR)/codecheck1 $(TRANS_SRC)
	$(TCC) -o $(APPNAME) $(OBJ) $(EXTRAOBJ) $(LIB)

# This rule prevents make from using its default rules to try build
# an executable named "manifest" out of the file named "manifest.c"
#
$(SRCDIR)/../manifest:
	# noop

clean:
	rm -rf $(OBJDIR)/* $(APPNAME)

}

set mhargs {}
foreach s [lsort $src] {
  append mhargs "\$(OBJDIR)/${s}_.c:\$(OBJDIR)/$s.h <<<NEXT_LINE>>>"
  set extra_h($s) { }
}
append mhargs "\$(SRCDIR)/sqlite3.h <<<NEXT_LINE>>>"
append mhargs "\$(SRCDIR)/th.h <<<NEXT_LINE>>>"
#append mhargs "\$(SRCDIR)/cson_amalgamation.h <<<NEXT_LINE>>>"
append mhargs "\$(OBJDIR)/VERSION.h"
set mhargs [string map [list <<<NEXT_LINE>>> \\\n\t] $mhargs]
writeln "\$(OBJDIR)/page_index.h: \$(TRANS_SRC) \$(OBJDIR)/mkindex"
writeln "\t\$(OBJDIR)/mkindex \$(TRANS_SRC) >\$@\n"

writeln "\$(OBJDIR)/builtin_data.h: \$(OBJDIR)/mkbuiltin \$(EXTRA_FILES)"
writeln "\t\$(OBJDIR)/mkbuiltin --prefix \$(SRCDIR)/ \$(EXTRA_FILES) >\$@\n"

writeln "\$(OBJDIR)/headers:\t\$(OBJDIR)/page_index.h \$(OBJDIR)/builtin_data.h \$(OBJDIR)/makeheaders \$(OBJDIR)/VERSION.h"
writeln "\t\$(OBJDIR)/makeheaders $mhargs"
writeln "\ttouch \$(OBJDIR)/headers"
writeln "\$(OBJDIR)/headers: Makefile"
writeln "\$(OBJDIR)/json.o \$(OBJDIR)/json_artifact.o \$(OBJDIR)/json_branch.o \$(OBJDIR)/json_config.o \$(OBJDIR)/json_diff.o \$(OBJDIR)/json_dir.o \$(OBJDIR)/json_finfo.o \$(OBJDIR)/json_login.o \$(OBJDIR)/json_query.o \$(OBJDIR)/json_report.o \$(OBJDIR)/json_status.o \$(OBJDIR)/json_tag.o \$(OBJDIR)/json_timeline.o \$(OBJDIR)/json_user.o \$(OBJDIR)/json_wiki.o : \$(SRCDIR)/json_detail.h"
writeln "Makefile:"
set extra_h(dispatch) " \$(OBJDIR)/page_index.h "
set extra_h(builtin) " \$(OBJDIR)/builtin_data.h "

foreach s [lsort $src] {
  writeln "\$(OBJDIR)/${s}_.c:\t\$(SRCDIR)/$s.c \$(OBJDIR)/translate"
  writeln "\t\$(OBJDIR)/translate \$(SRCDIR)/$s.c >\$@\n"
  writeln "\$(OBJDIR)/$s.o:\t\$(OBJDIR)/${s}_.c \$(OBJDIR)/$s.h$extra_h($s)\$(SRCDIR)/config.h"
  writeln "\t\$(XTCC) -o \$(OBJDIR)/$s.o -c \$(OBJDIR)/${s}_.c\n"
  writeln "\$(OBJDIR)/$s.h:\t\$(OBJDIR)/headers\n"
}

writeln "\$(OBJDIR)/sqlite3.o:\t\$(SQLITE3_SRC)"
writeln "\t\$(XTCC) \$(SQLITE_OPTIONS) \$(SQLITE_CFLAGS) \$(SEE_FLAGS) \\"
writeln "\t\t-c \$(SQLITE3_SRC) -o \$@"

writeln "\$(OBJDIR)/shell.o:\t\$(SQLITE3_SHELL_SRC) \$(SRCDIR)/sqlite3.h"
writeln "\t\$(XTCC) \$(SHELL_OPTIONS) \$(SHELL_CFLAGS) \$(SEE_FLAGS) \$(LINENOISE_DEF.\$(USE_LINENOISE)) -c \$(SQLITE3_SHELL_SRC) -o \$@\n"

writeln "\$(OBJDIR)/linenoise.o:\t\$(SRCDIR)/linenoise.c \$(SRCDIR)/linenoise.h"
writeln "\t\$(XTCC) -c \$(SRCDIR)/linenoise.c -o \$@\n"

writeln "\$(OBJDIR)/th.o:\t\$(SRCDIR)/th.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th.c -o \$@\n"

writeln "\$(OBJDIR)/th_lang.o:\t\$(SRCDIR)/th_lang.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_lang.c -o \$@\n"

writeln "\$(OBJDIR)/th_tcl.o:\t\$(SRCDIR)/th_tcl.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_tcl.c -o \$@\n"

writeln {
$(OBJDIR)/miniz.o:	$(SRCDIR)/miniz.c
	$(XTCC) $(MINIZ_OPTIONS) -c $(SRCDIR)/miniz.c -o $@

$(OBJDIR)/cson_amalgamation.o: $(SRCDIR)/cson_amalgamation.c
	$(XTCC) -c $(SRCDIR)/cson_amalgamation.c -o $@

#
# The list of all the targets that do not correspond to real files. This stops
# 'make' from getting confused when someone makes an error in a rule.
#

.PHONY: all install test clean
}

close $output_file
#
# End of the main.mk output
##############################################################################
##############################################################################
##############################################################################
# Begin win/Makefile.mingw output
#
puts "building ../win/Makefile.mingw"
set output_file [open ../win/Makefile.mingw w]
fconfigure $output_file -translation binary

writeln {#!/usr/bin/make
#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "src/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
# This is a makefile for use on Cygwin/Darwin/FreeBSD/Linux/Windows using
# MinGW or MinGW-w64.
#
# Some of the special options which can be passed to make
#   USE_WINDOWS=1    if building under a windows command prompt
#   X64=1            if using an unprefixed 64-bit mingw compiler
#

#### Select one of MinGW, MinGW-w64 (32-bit) or MinGW-w64 (64-bit) compilers.
#    By default, this is an empty string (i.e. use the native compiler).
#
PREFIX =
# PREFIX = mingw32-
# PREFIX = i686-pc-mingw32-
# PREFIX = i686-w64-mingw32-
# PREFIX = x86_64-w64-mingw32-

#### The toplevel directory of the source tree.  Fossil can be built
#    in a directory that is separate from the source tree.  Just change
#    the following to point from the build directory to the src/ folder.
#
SRCDIR = src

#### The directory into which object code files should be written.
#
OBJDIR = wbld

#### C compiler for use in building executables that will run on
#    the platform that is doing the build.  This is used to compile
#    code-generator programs as part of the build process.  See TCC
#    and TCCEXE below for the C compiler for building the finished
#    binary.
#
BCCEXE = gcc

#### C Compiler and options for use in building executables that
#    will run on the platform that is doing the build.  This is used
#    to compile code-generator programs as part of the build process.
#    See TCC below for the C compiler for building the finished binary.
#
BCC = $(BCCEXE)

#### Enable compiling with debug symbols (much larger binary)
#
# FOSSIL_ENABLE_SYMBOLS = 1

#### Enable JSON (http://www.json.org) support using "cson"
#
# FOSSIL_ENABLE_JSON = 1

#### Enable HTTPS support via OpenSSL (links to libssl and libcrypto)
#
# FOSSIL_ENABLE_SSL = 1

#### Automatically build OpenSSL when building Fossil (causes rebuild
#    issues when building incrementally).
#
# FOSSIL_BUILD_SSL = 1

#### Enable relative paths in external diff/gdiff
#
# FOSSIL_ENABLE_EXEC_REL_PATHS = 1

#### Enable legacy treatment of mv/rm (skip checkout files)
#
# FOSSIL_ENABLE_LEGACY_MV_RM = 1

#### Enable TH1 scripts in embedded documentation files
#
# FOSSIL_ENABLE_TH1_DOCS = 1

#### Enable hooks for commands and web pages via TH1
#
# FOSSIL_ENABLE_TH1_HOOKS = 1

#### Enable scripting support via Tcl/Tk
#
# FOSSIL_ENABLE_TCL = 1

#### Load Tcl using the stubs library mechanism
#
# FOSSIL_ENABLE_TCL_STUBS = 1

#### Load Tcl using the private stubs mechanism
#
# FOSSIL_ENABLE_TCL_PRIVATE_STUBS = 1

#### Use 'system' SQLite
#
# USE_SYSTEM_SQLITE = 1

#### Use the SQLite Encryption Extension
#
# USE_SEE = 1

#### Use the miniz compression library
#
# FOSSIL_ENABLE_MINIZ = 1

#### Use the Tcl source directory instead of the install directory?
#    This is useful when Tcl has been compiled statically with MinGW.
#
FOSSIL_TCL_SOURCE = 1

#### Check if the workaround for the MinGW command line handling needs to
#    be enabled by default.  This check may be somewhat fragile due to the
#    use of "findstring".
#
ifndef MINGW_IS_32BIT_ONLY
ifeq (,$(findstring w64-mingw32,$(PREFIX)))
MINGW_IS_32BIT_ONLY = 1
endif
endif

#### The directories where the zlib include and library files are located.
#
ZINCDIR = $(SRCDIR)/../compat/zlib
ZLIBDIR = $(SRCDIR)/../compat/zlib

#### Make an attempt to detect if Fossil is being built for the x64 processor
#    architecture.  This check may be somewhat fragile due to "findstring".
#
ifndef X64
ifneq (,$(findstring x86_64-w64-mingw32,$(PREFIX)))
X64 = 1
endif
endif

#### Determine if the optimized assembly routines provided with zlib should be
#    used, taking into account whether zlib is actually enabled and the target
#    processor architecture.
#
ifndef X64
SSLCONFIG = mingw
ifndef FOSSIL_ENABLE_MINIZ
ZLIBCONFIG = LOC="-DASMV -DASMINF" OBJA="inffas86.o match.o"
ZLIBTARGETS = $(ZLIBDIR)/inffas86.o $(ZLIBDIR)/match.o
else
ZLIBCONFIG =
ZLIBTARGETS =
endif
else
SSLCONFIG = mingw64
ZLIBCONFIG =
ZLIBTARGETS =
endif

#### Disable creation of the OpenSSL shared libraries.  Also, disable support
#    for both SSLv2 and SSLv3 (i.e. thereby forcing the use of TLS).
#
SSLCONFIG += no-ssl2 no-ssl3 no-shared

#### When using zlib, make sure that OpenSSL is configured to use the zlib
#    that Fossil knows about (i.e. the one within the source tree).
#
ifndef FOSSIL_ENABLE_MINIZ
SSLCONFIG +=  --with-zlib-lib=$(PWD)/$(ZLIBDIR) --with-zlib-include=$(PWD)/$(ZLIBDIR) zlib
endif

#### The directories where the OpenSSL include and library files are located.
#    The recommended usage here is to use the Sysinternals junction tool
#    to create a hard link between an "openssl-1.x" sub-directory of the
#    Fossil source code directory and the target OpenSSL source directory.
#
OPENSSLDIR = $(SRCDIR)/../compat/openssl-1.0.2l
OPENSSLINCDIR = $(OPENSSLDIR)/include
OPENSSLLIBDIR = $(OPENSSLDIR)

#### Either the directory where the Tcl library is installed or the Tcl
#    source code directory resides (depending on the value of the macro
#    FOSSIL_TCL_SOURCE).  If this points to the Tcl install directory,
#    this directory must have "include" and "lib" sub-directories.  If
#    this points to the Tcl source code directory, this directory must
#    have "generic" and "win" sub-directories.  The recommended usage
#    here is to use the Sysinternals junction tool to create a hard
#    link between a "tcl-8.x" sub-directory of the Fossil source code
#    directory and the target Tcl directory.  This removes the need to
#    hard-code the necessary paths in this Makefile.
#
TCLDIR = $(SRCDIR)/../compat/tcl-8.6

#### The Tcl source code directory.  This defaults to the same value as
#    TCLDIR macro (above), which may not be correct.  This value will
#    only be used if the FOSSIL_TCL_SOURCE macro is defined.
#
TCLSRCDIR = $(TCLDIR)

#### The Tcl include and library directories.  These values will only be
#    used if the FOSSIL_TCL_SOURCE macro is not defined.
#
TCLINCDIR = $(TCLDIR)/include
TCLLIBDIR = $(TCLDIR)/lib

#### Tcl: Which Tcl library do we want to use (8.4, 8.5, 8.6, etc)?
#
ifdef FOSSIL_ENABLE_TCL_STUBS
ifndef FOSSIL_ENABLE_TCL_PRIVATE_STUBS
LIBTCL = -ltclstub86
endif
TCLTARGET = libtclstub86.a
else
LIBTCL = -ltcl86
TCLTARGET = binaries
endif

#### C compiler for use in building executables that will run on the
#    target platform.  This is usually the same as BCCEXE, unless you
#    are cross-compiling.  This C compiler builds the finished binary
#    for fossil.  See BCC and BCCEXE above for the C compiler for
#    building intermediate code-generator tools.
#
TCCEXE = gcc

#### C compiler and options for use in building executables that will
#    run on the target platform.  This is usually the almost the same
#    as BCC, unless you are cross-compiling.  This C compiler builds
#    the finished binary for fossil.  The BCC compiler above is used
#    for building intermediate code-generator tools.
#
TCC = $(PREFIX)$(TCCEXE) -Wall -Wdeclaration-after-statement

#### Add the necessary command line options to build with debugging
#    symbols, if enabled.
#
ifdef FOSSIL_ENABLE_SYMBOLS
TCC += -g
else
TCC += -Os
endif

#### When not using the miniz compression library, zlib is required.
#
ifndef FOSSIL_ENABLE_MINIZ
TCC += -L$(ZLIBDIR) -I$(ZINCDIR)
endif

#### Compile resources for use in building executables that will run
#    on the target platform.
#
RCC = $(PREFIX)windres -I$(SRCDIR)

ifndef FOSSIL_ENABLE_MINIZ
RCC += -I$(ZINCDIR)
endif

# With HTTPS support
ifdef FOSSIL_ENABLE_SSL
TCC += -L$(OPENSSLLIBDIR) -I$(OPENSSLINCDIR)
RCC += -I$(OPENSSLINCDIR)
endif

# With Tcl support
ifdef FOSSIL_ENABLE_TCL
ifdef FOSSIL_TCL_SOURCE
TCC += -L$(TCLSRCDIR)/win -I$(TCLSRCDIR)/generic -I$(TCLSRCDIR)/win
RCC += -I$(TCLSRCDIR)/generic -I$(TCLSRCDIR)/win
else
TCC += -L$(TCLLIBDIR) -I$(TCLINCDIR)
RCC += -I$(TCLINCDIR)
endif
endif

# With miniz (i.e. instead of zlib)
ifdef FOSSIL_ENABLE_MINIZ
TCC += -DFOSSIL_ENABLE_MINIZ=1
RCC += -DFOSSIL_ENABLE_MINIZ=1
endif

# With MinGW command line handling workaround
ifdef MINGW_IS_32BIT_ONLY
TCC += -DBROKEN_MINGW_CMDLINE=1
RCC += -DBROKEN_MINGW_CMDLINE=1
endif

# With HTTPS support
ifdef FOSSIL_ENABLE_SSL
TCC += -DFOSSIL_ENABLE_SSL=1
RCC += -DFOSSIL_ENABLE_SSL=1
endif

# With relative paths in external diff/gdiff
ifdef FOSSIL_ENABLE_EXEC_REL_PATHS
TCC += -DFOSSIL_ENABLE_EXEC_REL_PATHS=1
RCC += -DFOSSIL_ENABLE_EXEC_REL_PATHS=1
endif

# With legacy treatment of mv/rm
ifdef FOSSIL_ENABLE_LEGACY_MV_RM
TCC += -DFOSSIL_ENABLE_LEGACY_MV_RM=1
RCC += -DFOSSIL_ENABLE_LEGACY_MV_RM=1
endif

# With TH1 embedded docs support
ifdef FOSSIL_ENABLE_TH1_DOCS
TCC += -DFOSSIL_ENABLE_TH1_DOCS=1
RCC += -DFOSSIL_ENABLE_TH1_DOCS=1
endif

# With TH1 hook support
ifdef FOSSIL_ENABLE_TH1_HOOKS
TCC += -DFOSSIL_ENABLE_TH1_HOOKS=1
RCC += -DFOSSIL_ENABLE_TH1_HOOKS=1
endif

# With Tcl support
ifdef FOSSIL_ENABLE_TCL
TCC += -DFOSSIL_ENABLE_TCL=1
RCC += -DFOSSIL_ENABLE_TCL=1
# Either statically linked or via stubs
ifdef FOSSIL_ENABLE_TCL_STUBS
TCC += -DFOSSIL_ENABLE_TCL_STUBS=1 -DUSE_TCL_STUBS
RCC += -DFOSSIL_ENABLE_TCL_STUBS=1 -DUSE_TCL_STUBS
ifdef FOSSIL_ENABLE_TCL_PRIVATE_STUBS
TCC += -DFOSSIL_ENABLE_TCL_PRIVATE_STUBS=1
RCC += -DFOSSIL_ENABLE_TCL_PRIVATE_STUBS=1
endif
else
TCC += -DSTATIC_BUILD
RCC += -DSTATIC_BUILD
endif
endif

# With JSON support
ifdef FOSSIL_ENABLE_JSON
TCC += -DFOSSIL_ENABLE_JSON=1
RCC += -DFOSSIL_ENABLE_JSON=1
endif

# With SQLite Encryption Extension support
ifdef USE_SEE
TCC += -DUSE_SEE=1
RCC += -DUSE_SEE=1
endif

#### The option -static has no effect on MinGW(-w64), only dynamic
#    executables can be built when linking with MSVCRT.  OpenSSL
#    (optional) and zlib (required) however are always linked in
#    statically.  Therefore, the FOSSIL_DYNAMIC_BUILD option does
#    not really apply to MinGW (i.e. since ALL external libraries
#    are NOT linked dynamically).
#
# LIB = -static

#### MinGW: If available, use the Unicode capable runtime startup code.
#
ifndef MINGW_IS_32BIT_ONLY
LIB += -municode
endif

#### SQLite: If enabled, use the system SQLite library.
#
ifdef USE_SYSTEM_SQLITE
LIB += -lsqlite3
endif

#### OpenSSL: Add the necessary libraries required, if enabled.
#
ifdef FOSSIL_ENABLE_SSL
LIB += -lssl -lcrypto -lgdi32 -lcrypt32
endif

#### Tcl: Add the necessary libraries required, if enabled.
#
ifdef FOSSIL_ENABLE_TCL
LIB += $(LIBTCL)
endif

#### Extra arguments for linking the finished binary.  Fossil needs
#    to link against the Z-Lib compression library.  There are no
#    other mandatory dependencies.
#
LIB += -lmingwex

#### When not using the miniz compression library, zlib is required.
#
ifndef FOSSIL_ENABLE_MINIZ
LIB += -lz
endif

#### These libraries MUST appear in the same order as they do for Tcl
#    or linking with it will not work (exact reason unknown).
#
ifdef FOSSIL_ENABLE_TCL
ifdef FOSSIL_ENABLE_TCL_STUBS
LIB += -lkernel32 -lws2_32
else
LIB += -lnetapi32 -lkernel32 -luser32 -ladvapi32 -lws2_32
endif
else
LIB += -lkernel32 -lws2_32
endif

#### Tcl shell for use in running the fossil test suite.  This is only
#    used for testing.
#
TCLSH = tclsh

#### Nullsoft installer MakeNSIS location
#
MAKENSIS = "$(PROGRAMFILES)\NSIS\MakeNSIS.exe"

#### Inno Setup executable location
#
INNOSETUP = "$(PROGRAMFILES)\Inno Setup 5\ISCC.exe"

#### Include a configuration file that can override any one of these settings.
#
-include config.w32

# STOP HERE
# You should not need to change anything below this line
#--------------------------------------------------------
XBCC = $(BCC) $(CFLAGS)
XTCC = $(TCC) $(CFLAGS) -I. -I$(SRCDIR)
}
writeln -nonewline "SRC ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n  \$(SRCDIR)/$s.c"
}
writeln "\n"
writeln -nonewline "EXTRA_FILES ="
foreach s [lsort $extra_files] {
  writeln -nonewline " \\\n  \$(SRCDIR)/$s"
}
writeln "\n"
writeln -nonewline "TRANS_SRC ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n  \$(OBJDIR)/${s}_.c"
}
writeln "\n"
writeln -nonewline "OBJ ="
foreach s [lsort $src] {
  writeln -nonewline " \\\n \$(OBJDIR)/$s.o"
}
writeln "\n"
writeln "APPNAME    = ${name}.exe"
writeln "APPTARGETS ="
writeln {
#### If the USE_WINDOWS variable exists, it is assumed that we are building
#    inside of a Windows-style shell; otherwise, it is assumed that we are
#    building inside of a Unix-style shell.  Note that the "move" command is
#    broken when attempting to use it from the Windows shell via MinGW make
#    because the SHELL variable is only used for certain commands that are
#    recognized internally by make.
#
ifdef USE_WINDOWS
TRANSLATE   = $(subst /,\,$(OBJDIR)/translate.exe)
MAKEHEADERS = $(subst /,\,$(OBJDIR)/makeheaders.exe)
MKINDEX     = $(subst /,\,$(OBJDIR)/mkindex.exe)
MKBUILTIN   = $(subst /,\,$(OBJDIR)/mkbuiltin.exe)
MKVERSION   = $(subst /,\,$(OBJDIR)/mkversion.exe)
CODECHECK1  = $(subst /,\,$(OBJDIR)/codecheck1.exe)
CAT         = type
CP          = copy
GREP        = find
MV          = copy
RM          = del /Q
MKDIR       = -mkdir
RMDIR       = rmdir /S /Q
else
TRANSLATE   = $(OBJDIR)/translate.exe
MAKEHEADERS = $(OBJDIR)/makeheaders.exe
MKINDEX     = $(OBJDIR)/mkindex.exe
MKBUILTIN   = $(OBJDIR)/mkbuiltin.exe
MKVERSION   = $(OBJDIR)/mkversion.exe
CODECHECK1  = $(OBJDIR)/codecheck1.exe
CAT         = cat
CP          = cp
GREP        = grep
MV          = mv
RM          = rm -f
MKDIR       = -mkdir -p
RMDIR       = rm -rf
endif}

writeln {
all:	$(OBJDIR) $(APPNAME)

$(OBJDIR)/fossil.o:	$(SRCDIR)/../win/fossil.rc $(OBJDIR)/VERSION.h
ifdef USE_WINDOWS
	$(CAT) $(subst /,\,$(SRCDIR)\miniz.c) | $(GREP) "define MZ_VERSION" > $(subst /,\,$(OBJDIR)\minizver.h)
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.rc) $(subst /,\,$(OBJDIR))
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.ico) $(subst /,\,$(OBJDIR))
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.exe.manifest) $(subst /,\,$(OBJDIR))
else
	$(CAT) $(SRCDIR)/miniz.c | $(GREP) "define MZ_VERSION" > $(OBJDIR)/minizver.h
	$(CP) $(SRCDIR)/../win/fossil.rc $(OBJDIR)
	$(CP) $(SRCDIR)/../win/fossil.ico $(OBJDIR)
	$(CP) $(SRCDIR)/../win/fossil.exe.manifest $(OBJDIR)
endif
	$(RCC) $(OBJDIR)/fossil.rc -o $(OBJDIR)/fossil.o

install:	$(OBJDIR) $(APPNAME)
ifdef USE_WINDOWS
	$(MKDIR) $(subst /,\,$(INSTALLDIR))
	$(MV) $(subst /,\,$(APPNAME)) $(subst /,\,$(INSTALLDIR))
else
	$(MKDIR) $(INSTALLDIR)
	$(MV) $(APPNAME) $(INSTALLDIR)
endif

$(OBJDIR):
ifdef USE_WINDOWS
	$(MKDIR) $(subst /,\,$(OBJDIR))
else
	$(MKDIR) $(OBJDIR)
endif

$(TRANSLATE):	$(SRCDIR)/translate.c
	$(XBCC) -o $@ $(SRCDIR)/translate.c

$(MAKEHEADERS):	$(SRCDIR)/makeheaders.c
	$(XBCC) -o $@ $(SRCDIR)/makeheaders.c

$(MKINDEX):	$(SRCDIR)/mkindex.c
	$(XBCC) -o $@ $(SRCDIR)/mkindex.c

$(MKBUILTIN):	$(SRCDIR)/mkbuiltin.c
	$(XBCC) -o $@ $(SRCDIR)/mkbuiltin.c

$(MKVERSION): $(SRCDIR)/mkversion.c
	$(XBCC) -o $@ $(SRCDIR)/mkversion.c

$(CODECHECK1):	$(SRCDIR)/codecheck1.c
	$(XBCC) -o $@ $(SRCDIR)/codecheck1.c

# WARNING. DANGER. Running the test suite modifies the repository the
# build is done from, i.e. the checkout belongs to. Do not sync/push
# the repository after running the tests.
test:	$(OBJDIR) $(APPNAME)
	$(TCLSH) $(SRCDIR)/../test/tester.tcl $(APPNAME)

$(OBJDIR)/VERSION.h:	$(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(MKVERSION)
	$(MKVERSION) $(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(SRCDIR)/../VERSION >$@

# The USE_SYSTEM_SQLITE variable may be undefined, set to 0, or set
# to 1. If it is set to 1, then there is no need to build or link
# the sqlite3.o object. Instead, the system SQLite will be linked
# using -lsqlite3.
SQLITE3_OBJ.0 = $(OBJDIR)/sqlite3.o
SQLITE3_OBJ.1 =
SQLITE3_OBJ.  = $(SQLITE3_OBJ.0)

# The FOSSIL_ENABLE_MINIZ variable may be undefined, set to 0, or
# set to 1.  If it is set to 1, the miniz library included in the
# source tree should be used; otherwise, it should not.
MINIZ_OBJ.0 =
MINIZ_OBJ.1 = $(OBJDIR)/miniz.o
MINIZ_OBJ.  = $(MINIZ_OBJ.0)

# The USE_SEE variable may be undefined, 0 or 1.  If undefined or
# 0, ordinary SQLite is used.  If 1, then sqlite3-see.c (not part of
# the source tree) is used and extra flags are provided to enable
# the SQLite Encryption Extension.
SQLITE3_SRC.0 = sqlite3.c
SQLITE3_SRC.1 = sqlite3-see.c
SQLITE3_SRC. = sqlite3.c
SQLITE3_SRC = $(SRCDIR)/$(SQLITE3_SRC.$(USE_SEE))
SQLITE3_SHELL_SRC.0 = shell.c
SQLITE3_SHELL_SRC.1 = shell-see.c
SQLITE3_SHELL_SRC. = shell.c
SQLITE3_SHELL_SRC = $(SRCDIR)/$(SQLITE3_SHELL_SRC.$(USE_SEE))
SEE_FLAGS.0 =
SEE_FLAGS.1 = -DSQLITE_HAS_CODEC -DSQLITE_SHELL_DBKEY_PROC=fossil_key
SEE_FLAGS. =
SEE_FLAGS = $(SEE_FLAGS.$(USE_SEE))
}

writeln [string map [list <<<NEXT_LINE>>> \\] {
EXTRAOBJ = <<<NEXT_LINE>>>
 $(SQLITE3_OBJ.$(USE_SYSTEM_SQLITE)) <<<NEXT_LINE>>>
 $(MINIZ_OBJ.$(FOSSIL_ENABLE_MINIZ)) <<<NEXT_LINE>>>
 $(OBJDIR)/shell.o <<<NEXT_LINE>>>
 $(OBJDIR)/th.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_lang.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_tcl.o <<<NEXT_LINE>>>
 $(OBJDIR)/cson_amalgamation.o
}]

writeln {
$(ZLIBDIR)/inffas86.o:
	$(TCC) -c -o $@ -DASMINF -I$(ZLIBDIR) -O3 $(ZLIBDIR)/contrib/inflate86/inffas86.c

$(ZLIBDIR)/match.o:
	$(TCC) -c -o $@ -DASMV $(ZLIBDIR)/contrib/asm686/match.S

zlib:	$(ZLIBTARGETS)
	$(MAKE) -C $(ZLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) $(ZLIBCONFIG) -f win32/Makefile.gcc libz.a

clean-zlib:
	$(MAKE) -C $(ZLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) -f win32/Makefile.gcc clean

ifdef FOSSIL_ENABLE_MINIZ
BLDTARGETS =
else
BLDTARGETS = zlib
endif

openssl:	$(BLDTARGETS)
	cd $(OPENSSLLIBDIR);./Configure --cross-compile-prefix=$(PREFIX) $(SSLCONFIG)
	$(MAKE) -C $(OPENSSLLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) build_libs

clean-openssl:
	$(MAKE) -C $(OPENSSLLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) clean

tcl:
	cd $(TCLSRCDIR)/win;./configure
	$(MAKE) -C $(TCLSRCDIR)/win PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) $(TCLTARGET)

clean-tcl:
	$(MAKE) -C $(TCLSRCDIR)/win PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) distclean

APPTARGETS += $(BLDTARGETS)

ifdef FOSSIL_BUILD_SSL
APPTARGETS += openssl
endif

$(APPNAME):	$(APPTARGETS) $(OBJDIR)/headers $(CODECHECK1) $(OBJ) $(EXTRAOBJ) $(OBJDIR)/fossil.o
	$(CODECHECK1) $(TRANS_SRC)
	$(TCC) -o $@ $(OBJ) $(EXTRAOBJ) $(OBJDIR)/fossil.o $(LIB)

# This rule prevents make from using its default rules to try build
# an executable named "manifest" out of the file named "manifest.c"
#
$(SRCDIR)/../manifest:
	# noop

clean:
ifdef USE_WINDOWS
	$(RM) $(subst /,\,$(APPNAME))
	$(RMDIR) $(subst /,\,$(OBJDIR))
else
	$(RM) $(APPNAME)
	$(RMDIR) $(OBJDIR)
endif

setup: $(OBJDIR) $(APPNAME)
	$(MAKENSIS) ./setup/fossil.nsi

innosetup: $(OBJDIR) $(APPNAME)
	$(INNOSETUP) ./setup/fossil.iss -DAppVersion=$(shell $(CAT) ./VERSION)
}

set mhargs {}
foreach s [lsort $src] {
  if {[string length $mhargs] > 0} {append mhargs " \\\n\t\t"}
  append mhargs "\$(OBJDIR)/${s}_.c:\$(OBJDIR)/$s.h"
  set extra_h($s) { }
}
append mhargs " \\\n\t\t\$(SRCDIR)/sqlite3.h"
append mhargs " \\\n\t\t\$(SRCDIR)/th.h"
append mhargs " \\\n\t\t\$(OBJDIR)/VERSION.h"
writeln "\$(OBJDIR)/page_index.h: \$(TRANS_SRC) \$(MKINDEX)"
writeln "\t\$(MKINDEX) \$(TRANS_SRC) >\$@\n"

writeln "\$(OBJDIR)/builtin_data.h:\t\$(MKBUILTIN) \$(EXTRA_FILES)"
writeln "\t\$(MKBUILTIN) --prefix \$(SRCDIR)/ \$(EXTRA_FILES) >\$@\n"

writeln "\$(OBJDIR)/headers:\t\$(OBJDIR)/page_index.h \$(OBJDIR)/builtin_data.h \$(MAKEHEADERS) \$(OBJDIR)/VERSION.h"
writeln "\t\$(MAKEHEADERS) $mhargs"
writeln "\techo Done >\$(OBJDIR)/headers\n"
writeln "\$(OBJDIR)/headers: Makefile\n"
writeln "Makefile:\n"
set extra_h(main) " \$(OBJDIR)/page_index.h "
set extra_h(builtin) " \$(OBJDIR)/builtin_data.h "

foreach s [lsort $src] {
  writeln "\$(OBJDIR)/${s}_.c:\t\$(SRCDIR)/$s.c \$(TRANSLATE)"
  writeln "\t\$(TRANSLATE) \$(SRCDIR)/$s.c >\$@\n"
  writeln "\$(OBJDIR)/$s.o:\t\$(OBJDIR)/${s}_.c \$(OBJDIR)/$s.h$extra_h($s)\$(SRCDIR)/config.h"
  writeln "\t\$(XTCC) -o \$(OBJDIR)/$s.o -c \$(OBJDIR)/${s}_.c\n"
  writeln "\$(OBJDIR)/${s}.h:\t\$(OBJDIR)/headers\n"
}

writeln {MINGW_OPTIONS = -D_HAVE__MINGW_H
}

set SQLITE_WIN32_OPTIONS $SQLITE_OPTIONS
lappend SQLITE_WIN32_OPTIONS -DSQLITE_WIN32_NO_ANSI

set MINGW_SQLITE_OPTIONS $SQLITE_WIN32_OPTIONS
lappend MINGW_SQLITE_OPTIONS {$(MINGW_OPTIONS)}
lappend MINGW_SQLITE_OPTIONS -DSQLITE_USE_MALLOC_H
lappend MINGW_SQLITE_OPTIONS -DSQLITE_USE_MSIZE

set MINIZ_WIN32_OPTIONS $MINIZ_OPTIONS

set j " \\\n                 "
writeln "SQLITE_OPTIONS = [join $MINGW_SQLITE_OPTIONS $j]\n"
set j " \\\n                "
writeln "SHELL_OPTIONS = [join $SHELL_WIN32_OPTIONS $j]\n"
set j " \\\n                "
writeln "MINIZ_OPTIONS = [join $MINIZ_WIN32_OPTIONS $j]\n"

writeln "\$(OBJDIR)/sqlite3.o:\t\$(SQLITE3_SRC) \$(SRCDIR)/../win/Makefile.mingw"
writeln "\t\$(XTCC) \$(SQLITE_OPTIONS) \$(SQLITE_CFLAGS) \$(SEE_FLAGS) \\"
writeln "\t\t-c \$(SQLITE3_SRC) -o \$@\n"

writeln "\$(OBJDIR)/cson_amalgamation.o:\t\$(SRCDIR)/cson_amalgamation.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/cson_amalgamation.c -o \$@\n"
writeln "\$(OBJDIR)/json.o \$(OBJDIR)/json_artifact.o \$(OBJDIR)/json_branch.o \$(OBJDIR)/json_config.o \$(OBJDIR)/json_diff.o \$(OBJDIR)/json_dir.o \$(OBJDIR)/jsos_finfo.o \$(OBJDIR)/json_login.o \$(OBJDIR)/json_query.o \$(OBJDIR)/json_report.o \$(OBJDIR)/json_status.o \$(OBJDIR)/json_tag.o \$(OBJDIR)/json_timeline.o \$(OBJDIR)/json_user.o \$(OBJDIR)/json_wiki.o : \$(SRCDIR)/json_detail.h\n"

writeln "\$(OBJDIR)/shell.o:\t\$(SQLITE3_SHELL_SRC) \$(SRCDIR)/sqlite3.h \$(SRCDIR)/../win/Makefile.mingw"
writeln "\t\$(XTCC) \$(SHELL_OPTIONS) \$(SHELL_CFLAGS) \$(SEE_FLAGS) -c \$(SQLITE3_SHELL_SRC) -o \$@\n"

writeln "\$(OBJDIR)/th.o:\t\$(SRCDIR)/th.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th.c -o \$@\n"

writeln "\$(OBJDIR)/th_lang.o:\t\$(SRCDIR)/th_lang.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_lang.c -o \$@\n"

writeln "\$(OBJDIR)/th_tcl.o:\t\$(SRCDIR)/th_tcl.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_tcl.c -o \$@\n"

writeln "\$(OBJDIR)/miniz.o:\t\$(SRCDIR)/miniz.c"
writeln "\t\$(XTCC) \$(MINIZ_OPTIONS) -c \$(SRCDIR)/miniz.c -o \$@\n"

close $output_file
#
# End of the win/Makefile.mingw output
##############################################################################
##############################################################################
##############################################################################
# Begin win/Makefile.dmc output
#
puts "building ../win/Makefile.dmc"
set output_file [open ../win/Makefile.dmc w]
fconfigure $output_file -translation binary

writeln {#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "src/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
B      = ..
SRCDIR = $B\src
OBJDIR = .
O      = .obj
E      = .exe


# Maybe DMDIR, SSL or INCL needs adjustment
DMDIR  = c:\DM
INCL   = -I. -I$(SRCDIR) -I$B\win\include -I$(DMDIR)\extra\include

#SSL   =  -DFOSSIL_ENABLE_SSL=1
SSL    =

CFLAGS = -o
BCC    = $(DMDIR)\bin\dmc $(CFLAGS)
TCC    = $(DMDIR)\bin\dmc $(CFLAGS) $(DMCDEF) $(SSL) $(INCL)
LIBS   = $(DMDIR)\extra\lib\ zlib wsock32 advapi32
}
writeln "SQLITE_OPTIONS = [join $SQLITE_OPTIONS { }]\n"
writeln "SHELL_OPTIONS = [join $SHELL_WIN32_OPTIONS { }]\n"
writeln -nonewline "SRC   ="
foreach s [lsort $src] {
  writeln -nonewline " ${s}_.c"
}
writeln "\n"
writeln -nonewline "OBJ   = "
foreach s [lsort $src] {
  writeln -nonewline "\$(OBJDIR)\\$s\$O "
}
writeln "\$(OBJDIR)\\shell\$O \$(OBJDIR)\\sqlite3\$O \$(OBJDIR)\\th\$O \$(OBJDIR)\\th_lang\$O"
writeln {

RC=$(DMDIR)\bin\rcc
RCFLAGS=-32 -w1 -I$(SRCDIR) /D__DMC__

APPNAME = $(OBJDIR)\fossil$(E)

all: $(APPNAME)

$(APPNAME) : translate$E mkindex$E codecheck1$E headers  $(OBJ) $(OBJDIR)\link
	cd $(OBJDIR)
	codecheck1$E $(SRC)
	$(DMDIR)\bin\link @link

$(OBJDIR)\fossil.res:	$B\win\fossil.rc
	$(RC) $(RCFLAGS) -o$@ $**

$(OBJDIR)\link: $B\win\Makefile.dmc $(OBJDIR)\fossil.res}
writeln -nonewline "\t+echo "
foreach s [lsort $src] {
  writeln -nonewline "$s "
}
writeln "shell sqlite3 th th_lang > \$@"
writeln "\t+echo fossil >> \$@"
writeln "\t+echo fossil >> \$@"
writeln "\t+echo \$(LIBS) >> \$@"
writeln "\t+echo. >> \$@"
writeln "\t+echo fossil >> \$@"

writeln {
translate$E: $(SRCDIR)\translate.c
	$(BCC) -o$@ $**

makeheaders$E: $(SRCDIR)\makeheaders.c
	$(BCC) -o$@ $**

mkindex$E: $(SRCDIR)\mkindex.c
	$(BCC) -o$@ $**

mkbuiltin$E: $(SRCDIR)\mkbuiltin.c
	$(BCC) -o$@ $**

mkversion$E: $(SRCDIR)\mkversion.c
	$(BCC) -o$@ $**

codecheck1$E: $(SRCDIR)\codecheck1.c
	$(BCC) -o$@ $**

$(OBJDIR)\shell$O : $(SRCDIR)\shell.c
	$(TCC) -o$@ -c $(SHELL_OPTIONS) $(SQLITE_OPTIONS) $(SHELL_CFLAGS) $**

$(OBJDIR)\sqlite3$O : $(SRCDIR)\sqlite3.c
	$(TCC) -o$@ -c $(SQLITE_OPTIONS) $(SQLITE_CFLAGS) $**

$(OBJDIR)\th$O : $(SRCDIR)\th.c
	$(TCC) -o$@ -c $**

$(OBJDIR)\th_lang$O : $(SRCDIR)\th_lang.c
	$(TCC) -o$@ -c $**

$(OBJDIR)\cson_amalgamation.h : $(SRCDIR)\cson_amalgamation.h
	cp $@ $@

VERSION.h : mkversion$E $B\manifest.uuid $B\manifest $B\VERSION
	+$** > $@

page_index.h: mkindex$E $(SRC)
	+$** > $@

builtin_data.h:	mkbuiltin$E $(EXTRA_FILES)
	mkbuiltin$E --prefix $(SRCDIR)/ $(EXTRA_FILES) > $@

clean:
	-del $(OBJDIR)\*.obj
	-del *.obj *_.c *.h *.map

realclean:
	-del $(APPNAME) translate$E mkindex$E makeheaders$E mkversion$E codecheck1$E mkbuiltin$E

$(OBJDIR)\json$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_artifact$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_branch$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_config$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_diff$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_dir$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_finfo$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_login$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_query$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_report$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_status$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_tag$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_timeline$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_user$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_wiki$O : $(SRCDIR)\json_detail.h


}
foreach s [lsort $src] {
  writeln "\$(OBJDIR)\\$s\$O : ${s}_.c ${s}.h"
  writeln "\t\$(TCC) -o\$@ -c ${s}_.c\n"
  writeln "${s}_.c : \$(SRCDIR)\\$s.c"
  writeln "\t+translate\$E \$** > \$@\n"
}

writeln -nonewline "headers: makeheaders\$E page_index.h builtin_data.h VERSION.h\n\t +makeheaders\$E "
foreach s [lsort $src] {
  writeln -nonewline "${s}_.c:$s.h "
}
writeln "\$(SRCDIR)\\sqlite3.h \$(SRCDIR)\\th.h VERSION.h \$(SRCDIR)\\cson_amalgamation.h"
writeln "\t@copy /Y nul: headers"

close $output_file
#
# End of the win/Makefile.dmc output
##############################################################################
##############################################################################
##############################################################################
# Begin win/Makefile.msc output
#
puts "building ../win/Makefile.msc"
set output_file [open ../win/Makefile.msc w]
fconfigure $output_file -translation binary

writeln {#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "src/makemake.tcl")
##############################################################################
#
# This Makefile will only function correctly if used from a sub-directory
# that is a direct child of the top-level directory for this project.
#
!if !exist("..\.fossil-settings")
!error "Please change the current directory to the one containing this file."
!endif

#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
B       = ..
SRCDIR  = $B\src
OBJDIR  = .
OX      = .
O       = .obj
E       = .exe
P       = .pdb

# Perl is only necessary if OpenSSL support is enabled and it must
# be built from source code.  The PERLDIR variable should point to
# the directory containing the main Perl binary (i.e. "perl.exe").
PERLDIR = C:\Perl\bin
PERL    = perl.exe

# Enable debugging symbols?
!ifndef DEBUG
DEBUG = 0
!endif

# Build the OpenSSL libraries?
!ifndef FOSSIL_BUILD_SSL
FOSSIL_BUILD_SSL = 0
!endif

# Build the included zlib library?
!ifndef FOSSIL_BUILD_ZLIB
FOSSIL_BUILD_ZLIB = 1
!endif

# Link everything except SQLite dynamically?
!ifndef FOSSIL_DYNAMIC_BUILD
FOSSIL_DYNAMIC_BUILD = 0
!endif

# Enable relative paths in external diff/gdiff?
!ifndef FOSSIL_ENABLE_EXEC_REL_PATHS
FOSSIL_ENABLE_EXEC_REL_PATHS = 0
!endif

# Enable the JSON API?
!ifndef FOSSIL_ENABLE_JSON
FOSSIL_ENABLE_JSON = 0
!endif

# Enable legacy treatment of the mv/rm commands?
!ifndef FOSSIL_ENABLE_LEGACY_MV_RM
FOSSIL_ENABLE_LEGACY_MV_RM = 0
!endif

# Enable use of miniz instead of zlib?
!ifndef FOSSIL_ENABLE_MINIZ
FOSSIL_ENABLE_MINIZ = 0
!endif

# Enable OpenSSL support?
!ifndef FOSSIL_ENABLE_SSL
FOSSIL_ENABLE_SSL = 0
!endif

# Enable the Tcl integration subsystem?
!ifndef FOSSIL_ENABLE_TCL
FOSSIL_ENABLE_TCL = 0
!endif

# Enable TH1 scripts in embedded documentation files?
!ifndef FOSSIL_ENABLE_TH1_DOCS
FOSSIL_ENABLE_TH1_DOCS = 0
!endif

# Enable TH1 hooks for commands and web pages?
!ifndef FOSSIL_ENABLE_TH1_HOOKS
FOSSIL_ENABLE_TH1_HOOKS = 0
!endif

# Enable support for Windows XP with Visual Studio 201x?
!ifndef FOSSIL_ENABLE_WINXP
FOSSIL_ENABLE_WINXP = 0
!endif

# Enable support for the SQLite Encryption Extension?
!ifndef USE_SEE
USE_SEE = 0
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
SSLDIR    = $(B)\compat\openssl-1.0.2l
SSLINCDIR = $(SSLDIR)\inc32
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLLIBDIR = $(SSLDIR)\out32dll
!else
SSLLIBDIR = $(SSLDIR)\out32
!endif
SSLLFLAGS = /nologo /opt:ref /debug
SSLLIB    = ssleay32.lib libeay32.lib user32.lib gdi32.lib crypt32.lib
!if "$(PLATFORM)"=="amd64" || "$(PLATFORM)"=="x64"
!message Using 'x64' platform for OpenSSL...
# BUGBUG (OpenSSL): Using "no-ssl*" here breaks the build.
# SSLCONFIG = VC-WIN64A no-asm no-ssl2 no-ssl3
SSLCONFIG = VC-WIN64A no-asm
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
!endif
SSLSETUP  = ms\do_win64a.bat
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLNMAKE  = ms\ntdll.mak all
!else
SSLNMAKE  = ms\nt.mak all
!endif
# BUGBUG (OpenSSL): Using "OPENSSL_NO_SSL*" here breaks dynamic builds.
!if $(FOSSIL_DYNAMIC_BUILD)==0
SSLCFLAGS = -DOPENSSL_NO_SSL2 -DOPENSSL_NO_SSL3
!endif
!elseif "$(PLATFORM)"=="ia64"
!message Using 'ia64' platform for OpenSSL...
# BUGBUG (OpenSSL): Using "no-ssl*" here breaks the build.
# SSLCONFIG = VC-WIN64I no-asm no-ssl2 no-ssl3
SSLCONFIG = VC-WIN64I no-asm
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
!endif
SSLSETUP  = ms\do_win64i.bat
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLNMAKE  = ms\ntdll.mak all
!else
SSLNMAKE  = ms\nt.mak all
!endif
# BUGBUG (OpenSSL): Using "OPENSSL_NO_SSL*" here breaks dynamic builds.
!if $(FOSSIL_DYNAMIC_BUILD)==0
SSLCFLAGS = -DOPENSSL_NO_SSL2 -DOPENSSL_NO_SSL3
!endif
!else
!message Assuming 'x86' platform for OpenSSL...
# BUGBUG (OpenSSL): Using "no-ssl*" here breaks the build.
# SSLCONFIG = VC-WIN32 no-asm no-ssl2 no-ssl3
SSLCONFIG = VC-WIN32 no-asm
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
!endif
SSLSETUP  = ms\do_ms.bat
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLNMAKE  = ms\ntdll.mak all
!else
SSLNMAKE  = ms\nt.mak all
!endif
# BUGBUG (OpenSSL): Using "OPENSSL_NO_SSL*" here breaks dynamic builds.
!if $(FOSSIL_DYNAMIC_BUILD)==0
SSLCFLAGS = -DOPENSSL_NO_SSL2 -DOPENSSL_NO_SSL3
!endif
!endif
!endif

!if $(FOSSIL_ENABLE_TCL)!=0
TCLDIR    = $(B)\compat\tcl-8.6
TCLSRCDIR = $(TCLDIR)
TCLINCDIR = $(TCLSRCDIR)\generic
!endif

# zlib options
ZINCDIR   = $(B)\compat\zlib
ZLIBDIR   = $(B)\compat\zlib

!if $(FOSSIL_DYNAMIC_BUILD)!=0
ZLIB      = zdll.lib
!else
ZLIB      = zlib.lib
!endif

INCL      = /I. /I$(SRCDIR) /I$B\win\include

!if $(FOSSIL_ENABLE_MINIZ)==0
INCL      = $(INCL) /I$(ZINCDIR)
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
INCL      = $(INCL) /I$(SSLINCDIR)
!endif

!if $(FOSSIL_ENABLE_TCL)!=0
INCL      = $(INCL) /I$(TCLINCDIR)
!endif

CFLAGS    = /nologo
LDFLAGS   =

!if $(FOSSIL_DYNAMIC_BUILD)!=0
LDFLAGS   = $(LDFLAGS) /MANIFEST
!else
LDFLAGS   = $(LDFLAGS) /NODEFAULTLIB:msvcrt /MANIFEST:NO
!endif

!if $(FOSSIL_ENABLE_WINXP)!=0
XPCFLAGS  = $(XPCFLAGS) /D_WIN32_WINNT=0x0501 /D_USING_V110_SDK71_=1
CFLAGS    = $(CFLAGS) $(XPCFLAGS)
!if "$(PLATFORM)"=="amd64" || "$(PLATFORM)"=="x64"
XPLDFLAGS = $(XPLDFLAGS) /SUBSYSTEM:CONSOLE,5.02
!else
XPLDFLAGS = $(XPLDFLAGS) /SUBSYSTEM:CONSOLE,5.01
!endif
LDFLAGS   = $(LDFLAGS) $(XPLDFLAGS)
!endif

!if $(FOSSIL_DYNAMIC_BUILD)!=0
!if $(DEBUG)!=0
CRTFLAGS = /MDd
!else
CRTFLAGS = /MD
!endif
!else
!if $(DEBUG)!=0
CRTFLAGS = /MTd
!else
CRTFLAGS = /MT
!endif
!endif

!if $(DEBUG)!=0
CFLAGS    = $(CFLAGS) /Zi $(CRTFLAGS) /Od
LDFLAGS   = $(LDFLAGS) /DEBUG
!else
CFLAGS    = $(CFLAGS) $(CRTFLAGS) /O2
!endif

BCC       = $(CC) $(CFLAGS)
TCC       = $(CC) /c $(CFLAGS) $(MSCDEF) $(INCL)
RCC       = $(RC) /D_WIN32 /D_MSC_VER $(MSCDEF) $(INCL)
MTC       = mt
LIBS      = ws2_32.lib advapi32.lib
LIBDIR    =

!if $(FOSSIL_DYNAMIC_BUILD)!=0
TCC       = $(TCC) /DFOSSIL_DYNAMIC_BUILD=1
RCC       = $(RCC) /DFOSSIL_DYNAMIC_BUILD=1
!endif

!if $(FOSSIL_ENABLE_MINIZ)==0
LIBS      = $(LIBS) $(ZLIB)
LIBDIR    = $(LIBDIR) /LIBPATH:$(ZLIBDIR)
!endif

!if $(FOSSIL_ENABLE_MINIZ)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_MINIZ=1
RCC       = $(RCC) /DFOSSIL_ENABLE_MINIZ=1
!endif

!if $(FOSSIL_ENABLE_JSON)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_JSON=1
RCC       = $(RCC) /DFOSSIL_ENABLE_JSON=1
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_SSL=1
RCC       = $(RCC) /DFOSSIL_ENABLE_SSL=1
LIBS      = $(LIBS) $(SSLLIB)
LIBDIR    = $(LIBDIR) /LIBPATH:$(SSLLIBDIR)
!endif

!if $(FOSSIL_ENABLE_EXEC_REL_PATHS)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_EXEC_REL_PATHS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_EXEC_REL_PATHS=1
!endif

!if $(FOSSIL_ENABLE_LEGACY_MV_RM)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_LEGACY_MV_RM=1
RCC       = $(RCC) /DFOSSIL_ENABLE_LEGACY_MV_RM=1
!endif

!if $(FOSSIL_ENABLE_TH1_DOCS)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_TH1_DOCS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_TH1_DOCS=1
!endif

!if $(FOSSIL_ENABLE_TH1_HOOKS)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_TH1_HOOKS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_TH1_HOOKS=1
!endif

!if $(FOSSIL_ENABLE_TCL)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_TCL=1
RCC       = $(RCC) /DFOSSIL_ENABLE_TCL=1
TCC       = $(TCC) /DFOSSIL_ENABLE_TCL_STUBS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_TCL_STUBS=1
TCC       = $(TCC) /DFOSSIL_ENABLE_TCL_PRIVATE_STUBS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_TCL_PRIVATE_STUBS=1
TCC       = $(TCC) /DUSE_TCL_STUBS=1
RCC       = $(RCC) /DUSE_TCL_STUBS=1
!endif

!if $(USE_SEE)!=0
TCC       = $(TCC) /DUSE_SEE=1
RCC       = $(RCC) /DUSE_SEE=1
!endif
}
regsub -all {[-]D} [join $SQLITE_WIN32_OPTIONS { }] {/D} MSC_SQLITE_OPTIONS
set j " \\\n                 "
writeln "SQLITE_OPTIONS = [join $MSC_SQLITE_OPTIONS $j]\n"

regsub -all {[-]D} [join $SHELL_WIN32_OPTIONS { }] {/D} MSC_SHELL_OPTIONS
set j " \\\n                "
writeln "SHELL_OPTIONS = [join $MSC_SHELL_OPTIONS $j]\n"

regsub -all {[-]D} [join $MINIZ_WIN32_OPTIONS { }] {/D} MSC_MINIZ_OPTIONS
set j " \\\n                "
writeln "MINIZ_OPTIONS = [join $MSC_MINIZ_OPTIONS $j]\n"

writeln -nonewline "SRC   = "
set i 0
foreach s [lsort $src] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  writeln -nonewline "${s}_.c"; incr i
}
writeln "\n"
writeln -nonewline "EXTRA_FILES   = "
set i 0
foreach s [lsort $extra_files] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  writeln -nonewline "\$(SRCDIR)\\${s}"; incr i
}
writeln "\n"
set AdditionalObj [list shell sqlite3 th th_lang th_tcl cson_amalgamation]
writeln -nonewline "OBJ   = "
set i 0
foreach s [lsort [concat $src $AdditionalObj]] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  writeln -nonewline "\$(OX)\\$s\$O"; incr i
}
if {$i > 0} {
  writeln " \\"
}
writeln "!if \$(FOSSIL_ENABLE_MINIZ)!=0"
writeln -nonewline "        "
writeln "\$(OX)\\miniz\$O \\"; incr i
writeln "!endif"
writeln -nonewline "        \$(OX)\\fossil.res\n\n"
writeln [string map [list <<<NEXT_LINE>>> \\] {
APPNAME    = $(OX)\fossil$(E)
PDBNAME    = $(OX)\fossil$(P)
APPTARGETS =

all: $(OX) $(APPNAME)

zlib:
	@echo Building zlib from "$(ZLIBDIR)"...
!if $(FOSSIL_ENABLE_WINXP)!=0
	@pushd "$(ZLIBDIR)" && $(MAKE) /f win32\Makefile.msc $(ZLIB) "CC=cl $(XPCFLAGS)" "LD=link $(XPLDFLAGS)" && popd
!else
	@pushd "$(ZLIBDIR)" && $(MAKE) /f win32\Makefile.msc $(ZLIB) && popd
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
openssl:
	@echo Building OpenSSL from "$(SSLDIR)"...
!if "$(PERLDIR)" != ""
	@set PATH=$(PERLDIR);$(PATH)
!endif
	@pushd "$(SSLDIR)" && $(PERL) Configure $(SSLCONFIG) && popd
	@pushd "$(SSLDIR)" && call $(SSLSETUP) && popd
!if $(FOSSIL_ENABLE_WINXP)!=0
	@pushd "$(SSLDIR)" && $(MAKE) /f $(SSLNMAKE) "CC=cl $(SSLCFLAGS) $(XPCFLAGS)" "LFLAGS=$(SSLLFLAGS) $(XPLDFLAGS)" && popd
!else
	@pushd "$(SSLDIR)" && $(MAKE) /f $(SSLNMAKE) "CC=cl $(SSLCFLAGS)" && popd
!endif
!endif

!if $(FOSSIL_ENABLE_MINIZ)==0
!if $(FOSSIL_BUILD_ZLIB)!=0
APPTARGETS = $(APPTARGETS) zlib
!endif
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
!if $(FOSSIL_BUILD_SSL)!=0
APPTARGETS = $(APPTARGETS) openssl
!endif
!endif

$(APPNAME) : $(APPTARGETS) translate$E mkindex$E codecheck1$E headers $(OBJ) $(OX)\linkopts
	cd $(OX)
	codecheck1$E $(SRC)
	link $(LDFLAGS) /OUT:$@ $(LIBDIR) Wsetargv.obj fossil.res @linkopts
	if exist $@.manifest <<<NEXT_LINE>>>
		$(MTC) -nologo -manifest $@.manifest -outputresource:$@;1

$(OX)\linkopts: $B\win\Makefile.msc}]
set redir {>}
foreach s [lsort [concat $src $AdditionalObj]] {
  writeln "\techo \$(OX)\\$s.obj $redir \$@"
  set redir {>>}
}
set redir {>>}
writeln "!if \$(FOSSIL_ENABLE_MINIZ)!=0"
writeln "\techo \$(OX)\\miniz.obj $redir \$@"
writeln "!endif"
writeln "\techo \$(LIBS) $redir \$@"
writeln {
$(OX):
	@-mkdir $@

translate$E: $(SRCDIR)\translate.c
	$(BCC) $**

makeheaders$E: $(SRCDIR)\makeheaders.c
	$(BCC) $**

mkindex$E: $(SRCDIR)\mkindex.c
	$(BCC) $**

mkbuiltin$E: $(SRCDIR)\mkbuiltin.c
	$(BCC) $**

mkversion$E: $(SRCDIR)\mkversion.c
	$(BCC) $**

codecheck1$E: $(SRCDIR)\codecheck1.c
	$(BCC) $**

!if $(USE_SEE)!=0
SEE_FLAGS = /DSQLITE_HAS_CODEC=1 /DSQLITE_SHELL_DBKEY_PROC=fossil_key
SQLITE3_SHELL_SRC = $(SRCDIR)\shell-see.c
SQLITE3_SRC = $(SRCDIR)\sqlite3-see.c
!else
SEE_FLAGS =
SQLITE3_SHELL_SRC = $(SRCDIR)\shell.c
SQLITE3_SRC = $(SRCDIR)\sqlite3.c
!endif

$(OX)\shell$O : $(SQLITE3_SHELL_SRC) $B\win\Makefile.msc
	$(TCC) /Fo$@ $(SHELL_OPTIONS) $(SQLITE_OPTIONS) $(SHELL_CFLAGS) $(SEE_FLAGS) -c $(SQLITE3_SHELL_SRC)

$(OX)\sqlite3$O : $(SQLITE3_SRC) $B\win\Makefile.msc
	$(TCC) /Fo$@ -c $(SQLITE_OPTIONS) $(SQLITE_CFLAGS) $(SEE_FLAGS) $(SQLITE3_SRC)

$(OX)\th$O : $(SRCDIR)\th.c
	$(TCC) /Fo$@ -c $**

$(OX)\th_lang$O : $(SRCDIR)\th_lang.c
	$(TCC) /Fo$@ -c $**

$(OX)\th_tcl$O : $(SRCDIR)\th_tcl.c
	$(TCC) /Fo$@ -c $**

$(OX)\miniz$O : $(SRCDIR)\miniz.c
	$(TCC) /Fo$@ -c $(MINIZ_OPTIONS) $(SRCDIR)\miniz.c

VERSION.h : mkversion$E $B\manifest.uuid $B\manifest $B\VERSION
	$** > $@
$(OX)\cson_amalgamation$O : $(SRCDIR)\cson_amalgamation.c
	$(TCC) /Fo$@ /c $**

page_index.h: mkindex$E $(SRC)
	$** > $@

builtin_data.h:	mkbuiltin$E $(EXTRA_FILES)
	mkbuiltin$E --prefix $(SRCDIR)/ $(EXTRA_FILES) > $@

clean:
	del $(OX)\*.obj 2>NUL
	del *.obj 2>NUL
	del *_.c 2>NUL
	del *.h 2>NUL
	del *.ilk 2>NUL
	del *.map 2>NUL
	del *.res 2>NUL
	del headers 2>NUL
	del linkopts 2>NUL
	del vc*.pdb 2>NUL

realclean: clean
	del $(APPNAME) 2>NUL
	del $(PDBNAME) 2>NUL
	del translate$E 2>NUL
	del translate$P 2>NUL
	del mkindex$E 2>NUL
	del mkindex$P 2>NUL
	del makeheaders$E 2>NUL
	del makeheaders$P 2>NUL
	del mkversion$E 2>NUL
	del mkversion$P 2>NUL
	del codecheck1$E 2>NUL
	del codecheck1$P 2>NUL
	del mkbuiltin$E 2>NUL
	del mkbuiltin$P 2>NUL

$(OBJDIR)\json$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_artifact$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_branch$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_config$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_diff$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_dir$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_finfo$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_login$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_query$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_report$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_status$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_tag$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_timeline$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_user$O : $(SRCDIR)\json_detail.h
$(OBJDIR)\json_wiki$O : $(SRCDIR)\json_detail.h
}
foreach s [lsort $src] {
  writeln "\$(OX)\\$s\$O : ${s}_.c ${s}.h"
  writeln "\t\$(TCC) /Fo\$@ -c ${s}_.c\n"
  writeln "${s}_.c : \$(SRCDIR)\\$s.c"
  writeln "\ttranslate\$E \$** > \$@\n"
}

writeln "fossil.res : \$B\\win\\fossil.rc"
writeln "\t\$(RCC)  /fo \$@ \$**\n"

writeln "headers: makeheaders\$E page_index.h builtin_data.h VERSION.h"
writeln -nonewline "\tmakeheaders\$E "
set i 0
foreach s [lsort $src] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "\t\t\t"
  }
  writeln -nonewline "${s}_.c:$s.h"; incr i
}
writeln " \\\n\t\t\t\$(SRCDIR)\\sqlite3.h \\"
writeln "\t\t\t\$(SRCDIR)\\th.h \\"
writeln "\t\t\tVERSION.h \\"
writeln "\t\t\t\$(SRCDIR)\\cson_amalgamation.h"
writeln "\t@copy /Y nul: headers"


close $output_file
#
# End of the win/Makefile.msc output
##############################################################################
##############################################################################
##############################################################################
# Begin win/Makefile.PellesCGMake output
#
puts "building ../win/Makefile.PellesCGMake"
set output_file [open ../win/Makefile.PellesCGMake w]
fconfigure $output_file -translation binary

writeln [string map [list \
    <<<SQLITE_OPTIONS>>> [join $SQLITE_WIN32_OPTIONS { }] \
    <<<SHELL_OPTIONS>>> [join $SHELL_WIN32_OPTIONS { }]] {#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "src/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
# HowTo
# -----
#
# This is a Makefile to compile fossil with PellesC from
#  http://www.smorgasbordet.com/pellesc/index.htm
# In addition to the Compiler envrionment, you need
#  gmake from http://sourceforge.net/projects/unxutils/, Pelles make version
#        couldn't handle the complex dependencies in this build
#  zlib sources
# Then you do
# 1. create a directory PellesC in the project root directory
# 2. Change the variables PellesCDir/ZLIBSRCDIR to the path of your installation
# 3. open a dos prompt window and change working directory into PellesC (step 1)
# 4. run gmake -f ..\win\Makefile.PellesCGMake
#
# this file is tested with
#   PellesC         5.00.13
#   gmake           3.80
#   zlib sources    1.2.5
#   Windows XP SP 2
# and
#   PellesC         6.00.4
#   gmake           3.80
#   zlib sources    1.2.5
#   Windows 7 Home Premium
#

#
PellesCDir=c:\Programme\PellesC

# Select between 32/64 bit code, default is 32 bit
#TARGETVERSION=64

ifeq ($(TARGETVERSION),64)
# 64 bit version
TARGETMACHINE_CC=amd64
TARGETMACHINE_LN=amd64
TARGETEXTEND=64
else
# 32 bit version
TARGETMACHINE_CC=x86
TARGETMACHINE_LN=ix86
TARGETEXTEND=
endif

# define the project directories
B=..
SRCDIR=$(B)/src/
WINDIR=$(B)/win/
ZLIBSRCDIR=../../zlib/

# define linker command and options
LINK=$(PellesCDir)/bin/polink.exe
LINKFLAGS=-subsystem:console -machine:$(TARGETMACHINE_LN) /LIBPATH:$(PellesCDir)\lib\win$(TARGETEXTEND) /LIBPATH:$(PellesCDir)\lib kernel32.lib advapi32.lib delayimp$(TARGETEXTEND).lib Wsock32.lib Crtmt$(TARGETEXTEND).lib

# define standard C-compiler and flags, used to compile
# the fossil binary. Some special definitions follow for
# special files follow
CC=$(PellesCDir)\bin\pocc.exe
DEFINES=-D_pgmptr=g.argv[0]
CCFLAGS=-T$(TARGETMACHINE_CC)-coff -Ot -W2 -Gd -Go -Ze -MT $(DEFINES)
INCLUDE=/I $(PellesCDir)\Include\Win /I $(PellesCDir)\Include /I $(ZLIBSRCDIR) /I $(SRCDIR)

# define commands for building the windows resource files
RESOURCE=fossil.res
RC=$(PellesCDir)\bin\porc.exe
RCFLAGS=$(INCLUDE) -D__POCC__=1 -D_M_X$(TARGETVERSION)

# define the special utilities files, needed to generate
# the automatically generated source files
UTILS=translate.exe mkindex.exe makeheaders.exe mkbuiltin.exe
UTILS_OBJ=$(UTILS:.exe=.obj)
UTILS_SRC=$(foreach uf,$(UTILS),$(SRCDIR)$(uf:.exe=.c))

# define the SQLite files, which need special flags on compile
SQLITESRC=sqlite3.c
ORIGSQLITESRC=$(foreach sf,$(SQLITESRC),$(SRCDIR)$(sf))
SQLITEOBJ=$(foreach sf,$(SQLITESRC),$(sf:.c=.obj))
SQLITEDEFINES=<<<SQLITE_OPTIONS>>>

# define the SQLite shell files, which need special flags on compile
SQLITESHELLSRC=shell.c
ORIGSQLITESHELLSRC=$(foreach sf,$(SQLITESHELLSRC),$(SRCDIR)$(sf))
SQLITESHELLOBJ=$(foreach sf,$(SQLITESHELLSRC),$(sf:.c=.obj))
SQLITESHELLDEFINES=<<<SHELL_OPTIONS>>>

# define the th scripting files, which need special flags on compile
THSRC=th.c th_lang.c
ORIGTHSRC=$(foreach sf,$(THSRC),$(SRCDIR)$(sf))
THOBJ=$(foreach sf,$(THSRC),$(sf:.c=.obj))

# define the zlib files, needed by this compile
ZLIBSRC=adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c
ORIGZLIBSRC=$(foreach sf,$(ZLIBSRC),$(ZLIBSRCDIR)$(sf))
ZLIBOBJ=$(foreach sf,$(ZLIBSRC),$(sf:.c=.obj))

# define all fossil sources, using the standard compile and
# source generation. These are all files in SRCDIR, which are not
# mentioned as special files above:
ORIGSRC=$(filter-out $(UTILS_SRC) $(ORIGTHSRC) $(ORIGSQLITESRC) $(ORIGSQLITESHELLSRC),$(wildcard $(SRCDIR)*.c))
SRC=$(subst $(SRCDIR),,$(ORIGSRC))
TRANSLATEDSRC=$(SRC:.c=_.c)
TRANSLATEDOBJ=$(TRANSLATEDSRC:.c=.obj)

# main target file is the application
APPLICATION=fossil.exe

# define the standard make target
.PHONY:	default
default:	page_index.h builtin_data.h headers $(APPLICATION)

# symbolic target to generate the source generate utils
.PHONY:	utils
utils:	$(UTILS)

# link utils
$(UTILS) version.exe:	%.exe:	%.obj
	$(LINK) $(LINKFLAGS) -out:"$@" $<

# compiling standard fossil utils
$(UTILS_OBJ):	%.obj:	$(SRCDIR)%.c
	$(CC) $(CCFLAGS) $(INCLUDE) "$<" -Fo"$@"

# compile special windows utils
version.obj:	$(SRCDIR)mkversion.c
	$(CC) $(CCFLAGS) $(INCLUDE) "$<" -Fo"$@"

# generate the translated c-source files
$(TRANSLATEDSRC):	%_.c:	$(SRCDIR)%.c translate.exe
	translate.exe $< >$@

# generate the index source, containing all web references,..
page_index.h:	$(TRANSLATEDSRC) mkindex.exe
	mkindex.exe $(TRANSLATEDSRC) >$@

builtin_data.h:	$(EXTRA_FILES) mkbuiltin.exe
	mkbuiltin.exe --prefix $(SRCDIR)/ $(EXTRA_FILES) >$@

# extracting version info from manifest
VERSION.h:	version.exe ..\manifest.uuid ..\manifest ..\VERSION
	version.exe ..\manifest.uuid ..\manifest ..\VERSION  >$@

# generate the simplified headers
headers: makeheaders.exe page_index.h builtin_data.h VERSION.h ../src/sqlite3.h ../src/th.h VERSION.h
	makeheaders.exe $(foreach ts,$(TRANSLATEDSRC),$(ts):$(ts:_.c=.h)) ../src/sqlite3.h ../src/th.h VERSION.h
	echo Done >$@

# compile C sources with relevant options

$(TRANSLATEDOBJ):	%_.obj:	%_.c %.h
	$(CC) $(CCFLAGS) $(INCLUDE) "$<" -Fo"$@"

$(SQLITEOBJ):	%.obj:	$(SRCDIR)%.c $(SRCDIR)%.h
	$(CC) $(CCFLAGS) $(SQLITEDEFINES) $(INCLUDE) "$<" -Fo"$@"

$(SQLITESHELLOBJ):	%.obj:	$(SRCDIR)%.c
	$(CC) $(CCFLAGS) $(SQLITESHELLDEFINES) $(INCLUDE) "$<" -Fo"$@"

$(THOBJ):	%.obj:	$(SRCDIR)%.c $(SRCDIR)th.h
	$(CC) $(CCFLAGS) $(INCLUDE) "$<" -Fo"$@"

$(ZLIBOBJ):	%.obj:	$(ZLIBSRCDIR)%.c
	$(CC) $(CCFLAGS) $(INCLUDE) "$<" -Fo"$@"

# create the windows resource with icon and version info
$(RESOURCE):	%.res:	../win/%.rc ../win/*.ico
	$(RC) $(RCFLAGS) $< -Fo"$@"

# link the application
$(APPLICATION):	$(TRANSLATEDOBJ) $(SQLITEOBJ) $(SQLITESHELLOBJ) $(THOBJ) $(ZLIBOBJ) headers $(RESOURCE)
	$(LINK) $(LINKFLAGS) -out:"$@" $(TRANSLATEDOBJ) $(SQLITEOBJ) $(SQLITESHELLOBJ) $(THOBJ) $(ZLIBOBJ) $(RESOURCE)

# cleanup

.PHONY: clean
clean:
	del /F $(TRANSLATEDOBJ) $(SQLITEOBJ) $(THOBJ) $(ZLIBOBJ) $(UTILS_OBJ) version.obj
	del /F $(TRANSLATEDSRC)
	del /F *.h headers
	del /F $(RESOURCE)

.PHONY: clobber
clobber: clean
	del /F *.exe
}]
