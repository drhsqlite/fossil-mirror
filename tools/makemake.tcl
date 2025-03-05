#!/usr/bin/tclsh
#
#    ### Run this Tcl script EVERY time you modify it in any way! ###
#    ### It must be run from the directory it lives in so that    ###
#    ### directories resolve properly!                            ###
#
# This Tcl script generates make files for various platforms. The makefiles
# then need to be committed.
#
# If you modify this file then:
#
#     1. cd tools; tclsh makemake.tcl
#
#     2. if errors are reported, fix them and go to step 1
#
#     3. if "fossil diff" reports changes in any of the generated
#        files, commit the changed files to the repo
#
# Files generated include:
#
#     src/main.mk           # makefile for all unix systems
#     win/Makefile.mingw    # makefile for mingw on windows
#     win/Makefile.*        # makefiles for other windows compilers
#
# Add new source files by listing the files (without their .c suffix)
# in the "src" variable.  Add new resource files to the "extra_files"
# variable.  There are other variables that you can alter, down to
# the "STOP HERE" comment.  The stuff below "STOP HERE" should rarely need
# to change. After modification, go to step 1 above.
#
# Delete unused source files in the "src" variable, then go to step 1 above.
#
#############################################################################

# $srcDir is used to set the target source dir in several places. Not
# all code-generation bits use $srcDir and instead hard-code, so
# replacing it only here (should it ever changes) is not sufficient.
#
set srcDir ../src
# Directory $srcDirExt houses single-file source code solutions which
# are imported directly into the fossil source tree.
set srcDirExt ../extsrc

# Basenames of all source files that get preprocessed using
# "translate" and "makeheaders".  To add new C-language source files to the
# project, simply add the basename to this list and rerun this script.
#
# Set the separate extra_files variable further down for how to add non-C
# files, such as string and BLOB resources.
#

set src {
  add
  ajax
  alerts
  allrepo
  attach
  backlink
  backoffice
  bag
  bisect
  blob
  branch
  browse
  builtin
  bundle
  cache
  capabilities
  captcha
  cgi
  chat
  checkin
  checkout
  clearsign
  clone
  color
  comformat
  configure
  content
  cookies
  db
  delta
  deltacmd
  deltafunc
  descendants
  diff
  diffcmd
  dispatch
  doc
  encode
  etag
  event
  extcgi
  export
  file
  fileedit
  finfo
  foci
  forum
  fshell
  fusefs
  fuzz
  glob
  graph
  gzip
  hname
  hook
  http
  http_socket
  http_transport
  import
  info
  interwiki
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
  match
  md5
  merge
  merge3
  moderate
  name
  patch
  path
  piechart
  pikchrshow
  pivot
  popen
  pqueue
  printf
  publish
  purge
  rebuild
  regexp
  repolist
  report
  rss
  schema
  search
  security_audit
  setup
  setupuser
  sha1
  sha1hard
  sha3
  shun
  sitemap
  skins
  smtp
  sqlcmd
  stash
  stat
  statrep
  style
  sync
  tag
  tar
  terminal
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
  xfer
  xfersetup
  zip
  http_ssl
}

# Source files which live under $srcDirExt, but only those for which
# we need to run makeheaders. External sources which have their own
# header files must not be in this list.
set src_ext {
  pikchr
}

# Additional resource files that get built into the executable.
# These paths are all resolved from the src/ directory, so must
# be relative to that.
set extra_files {
  diff.tcl
  merge.tcl
  markdown.md
  wiki.wiki
  *.js
  default.css
  style.*.css
  ../skins/*/*.txt
  sounds/*.wav
  alerts/*.wav
  ../extsrc/pikchr.wasm
  ../extsrc/pikchr*.js
}

# Options used to compile the included SQLite library.
#
set SQLITE_OPTIONS {
  -DNDEBUG=1
  -DSQLITE_DQS=0
  -DSQLITE_THREADSAFE=0
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
  -DSQLITE_OMIT_DECLTYPE
  -DSQLITE_OMIT_DEPRECATED
  -DSQLITE_OMIT_PROGRESS_CALLBACK
  -DSQLITE_OMIT_SHARED_CACHE
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_MAX_EXPR_DEPTH=0
  -DSQLITE_ENABLE_LOCKING_STYLE=0
  -DSQLITE_DEFAULT_FILE_FORMAT=4
  -DSQLITE_ENABLE_DBSTAT_VTAB
  -DSQLITE_ENABLE_EXPLAIN_COMMENTS
  -DSQLITE_ENABLE_FTS4
  -DSQLITE_ENABLE_FTS5
  -DSQLITE_ENABLE_MATH_FUNCTIONS
  -DSQLITE_ENABLE_STMTVTAB
  -DSQLITE_HAVE_ZLIB
  -DSQLITE_ENABLE_DBPAGE_VTAB
  -DSQLITE_TRUSTED_SCHEMA=0
  -DHAVE_USLEEP
}
#lappend SQLITE_OPTIONS -DSQLITE_ENABLE_FTS3=1
#lappend SQLITE_OPTIONS -DSQLITE_ENABLE_STAT4
#lappend SQLITE_OPTIONS -DSQLITE_WIN32_NO_ANSI
#lappend SQLITE_OPTIONS -DSQLITE_WINNT_MAX_PATH_CHARS=4096

# Options used to compile the Pikchr library.
#
set PIKCHR_OPTIONS {
  -DPIKCHR_TOKEN_LIMIT=10000
}

# Options used to compile the included SQLite shell.
#
set SHELL_OPTIONS [concat $SQLITE_OPTIONS {
  -Dmain=sqlite3_shell
  -DSQLITE_SHELL_IS_UTF8=1
  -DSQLITE_OMIT_LOAD_EXTENSION=1
  -DUSE_SYSTEM_SQLITE=$(USE_SYSTEM_SQLITE)
  -DSQLITE_SHELL_DBNAME_PROC=sqlcmd_get_dbname
  -DSQLITE_SHELL_INIT_PROC=sqlcmd_init_proc
}]

# Options used to compile the included SQLite shell on Windows.
#
set SHELL_WIN32_OPTIONS $SHELL_OPTIONS
lappend SHELL_WIN32_OPTIONS -Daccess=file_access
lappend SHELL_WIN32_OPTIONS -Dsystem=fossil_system
lappend SHELL_WIN32_OPTIONS -Dgetenv=fossil_getenv
lappend SHELL_WIN32_OPTIONS -Dfopen=fossil_fopen

# STOP HERE.
# Unless the build procedures changes, you should not have to edit anything
# below this line.
#############################################################################

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

# Expand any wildcards in "extra_files"
set new_extra_files {}
foreach file $extra_files {
  # we need $file to resolve from $srcDir, but simply prepending
  # $srcDir to each name breaks how the names are stringified and
  # looked up from C.
  set cwd [pwd]
  cd $srcDir
  foreach x [glob $file] { # -nocomplain flag?
    lappend new_extra_files $x
  }
  cd $cwd
}
set extra_files $new_extra_files

##############################################################################
##############################################################################
##############################################################################
# Start by generating the "main.mk" makefile used for all unix systems.
#
set mainMk $srcDir/main.mk
puts "building $mainMk"
set output_file [open $mainMk w]
fconfigure $output_file -translation binary

writeln {#
##############################################################################
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "tools/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
# This file is included by primary Makefile.
#

XBCC = $(BCC) $(BCCFLAGS)
XTCC = $(TCC) $(CFLAGS_INCLUDE) -I$(OBJDIR) $(TCCFLAGS)

TESTFLAGS := -quiet
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

writeln [string map [list \
    <<<SQLITE_OPTIONS>>> [join $SQLITE_OPTIONS " \\\n                 "] \
    <<<SHELL_OPTIONS>>> [join $SHELL_OPTIONS " \\\n                "] \
    <<<PIKCHR_OPTIONS>>> [join $PIKCHR_OPTIONS " \\\n                "] \
    <<<NEXT_LINE>>> \\] {
all:	$(OBJDIR) $(APPNAME)

install:	all
	mkdir -p $(INSTALLDIR)
	cp $(APPNAME) $(INSTALLDIR)

codecheck:	$(TRANS_SRC) $(OBJDIR)/codecheck1
	$(OBJDIR)/codecheck1 $(TRANS_SRC)

$(OBJDIR):
	-mkdir $(OBJDIR)

$(OBJDIR)/translate:	$(SRCDIR_tools)/translate.c
	-mkdir -p $(OBJDIR)
	$(XBCC) -o $(OBJDIR)/translate $(SRCDIR_tools)/translate.c

$(OBJDIR)/makeheaders:	$(SRCDIR_tools)/makeheaders.c
	$(XBCC) -o $(OBJDIR)/makeheaders $(SRCDIR_tools)/makeheaders.c

$(OBJDIR)/mkindex:	$(SRCDIR_tools)/mkindex.c
	$(XBCC) -o $(OBJDIR)/mkindex $(SRCDIR_tools)/mkindex.c

$(OBJDIR)/mkbuiltin:	$(SRCDIR_tools)/mkbuiltin.c
	$(XBCC) -o $(OBJDIR)/mkbuiltin $(SRCDIR_tools)/mkbuiltin.c

$(OBJDIR)/mkversion:	$(SRCDIR_tools)/mkversion.c
	$(XBCC) -o $(OBJDIR)/mkversion $(SRCDIR_tools)/mkversion.c

$(OBJDIR)/codecheck1:	$(SRCDIR_tools)/codecheck1.c
	$(XBCC) -o $(OBJDIR)/codecheck1 $(SRCDIR_tools)/codecheck1.c

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
	$(TCLSH) $(SRCDIR)/../test/tester.tcl $(APPNAME) $(TESTFLAGS)

$(OBJDIR)/VERSION.h:	$(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(SRCDIR)/../VERSION $(OBJDIR)/mkversion $(OBJDIR)/phony.h
	$(OBJDIR)/mkversion $(SRCDIR)/../manifest.uuid <<<NEXT_LINE>>>
		$(SRCDIR)/../manifest <<<NEXT_LINE>>>
		$(SRCDIR)/../VERSION >$(OBJDIR)/VERSION.h

$(OBJDIR)/phony.h:
	# Force rebuild of VERSION.h every time we run "make"

# Setup the options used to compile the included SQLite library.
SQLITE_OPTIONS = <<<SQLITE_OPTIONS>>>

# Setup the options used to compile the included SQLite shell.
SHELL_OPTIONS = <<<SHELL_OPTIONS>>>

# Setup the options used to compile the included Pikchr formatter.
PIKCHR_OPTIONS = <<<PIKCHR_OPTIONS>>>

# The USE_SYSTEM_SQLITE variable may be undefined, set to 0 or 1.
# If it is set to 1, then there is no need to build or link
# the sqlite3.o object. Instead, the system SQLite will be linked
# using -lsqlite3.
#
# Closely related is SQLITE3_ORIGIN, with the same numeric mapping plus
# a value of 2 means that we are building a client-provided sqlite3.c.
SQLITE3_OBJ.0 = $(OBJDIR)/sqlite3.o
SQLITE3_OBJ.1 = $(OBJDIR)/sqlite3-see.o
# SQLITE3_OBJ.2 is set by the configure process
SQLITE3_OBJ.  = $(SQLITE3_OBJ.0)
SQLITE3_OBJ   = $(SQLITE3_OBJ.$(SQLITE3_ORIGIN))

# The USE_LINENOISE variable may be undefined, set to 0, or set
# to 1. If it is set to 0, then there is no need to build or link
# the linenoise.o object.
LINENOISE_DEF.0 =
LINENOISE_DEF.1 = -DHAVE_LINENOISE=2
LINENOISE_DEF.  = $(LINENOISE_DEF.0)
LINENOISE_OBJ.0 =
LINENOISE_OBJ.1 = $(OBJDIR)/linenoise.o
LINENOISE_OBJ.  = $(LINENOISE_OBJ.0)

# The USE_SEE variable may be undefined, 0 or 1.  If undefined or 0,
# in-tree SQLite is used.  If 1, then sqlite3-see.c (not part of the
# source tree) is used and extra flags are provided to enable the
# SQLite Encryption Extension.
SQLITE3_SRC.0 = $(SRCDIR_extsrc)/sqlite3.c
SQLITE3_SRC.1 = $(SRCDIR_extsrc)/sqlite3-see.c
# SQLITE3_SRC.2 is set by top-level configure/makefile process.
SQLITE3_SRC. = $(SRCDIR_extsrc)/sqlite3.c
SQLITE3_SRC = $(SQLITE3_SRC.$(SQLITE3_ORIGIN))
SQLITE3_SHELL_SRC.0 = $(SRCDIR_extsrc)/shell.c
SQLITE3_SHELL_SRC.1 = $(SRCDIR_extsrc)/shell-see.c
# SQLITE3_SHELL_SRC.2 comes from the configure process
SQLITE3_SHELL_SRC. = $(SRCDIR_extsrc)/shell.c
SQLITE3_SHELL_SRC = $(SQLITE3_SHELL_SRC.$(SQLITE3_ORIGIN))
SEE_FLAGS.0 =
SEE_FLAGS.1 = -DSQLITE_HAS_CODEC -DSQLITE_SHELL_DBKEY_PROC=fossil_key
SEE_FLAGS. =
SEE_FLAGS = $(SEE_FLAGS.$(USE_SEE))
}]

writeln [string map [list <<<NEXT_LINE>>> \\] {
EXTRAOBJ = <<<NEXT_LINE>>>
 $(SQLITE3_OBJ.$(SQLITE3_ORIGIN)) <<<NEXT_LINE>>>
 $(LINENOISE_OBJ.$(USE_LINENOISE)) <<<NEXT_LINE>>>
 $(OBJDIR)/pikchr.o <<<NEXT_LINE>>>
 $(OBJDIR)/shell.o <<<NEXT_LINE>>>
 $(OBJDIR)/th.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_lang.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_tcl.o <<<NEXT_LINE>>>
 $(OBJDIR)/cson_amalgamation.o
}]

writeln {
$(APPNAME):	$(OBJDIR)/headers $(OBJDIR)/codecheck1 $(EXTRAOBJ) $(OBJ)
	$(OBJDIR)/codecheck1 $(TRANS_SRC)
	$(TCC) $(TCCFLAGS) -o $(APPNAME) $(EXTRAOBJ) $(OBJ) $(LIB)

# This rule prevents make from using its default rules to try build
# an executable named "manifest" out of the file named "manifest.c"
#
$(SRCDIR)/../manifest:
	# noop

clean:
	-rm -rf $(OBJDIR)/* $(APPNAME)

}

set mhargs {}
foreach s [lsort $src] {
  append mhargs "\$(OBJDIR)/${s}_.c:\$(OBJDIR)/$s.h <<<NEXT_LINE>>>"
  set extra_h($s) { }
}
foreach s [lsort $src_ext] {
  append mhargs "\$(SRCDIR_extsrc)/${s}.c:\$(OBJDIR)/$s.h <<<NEXT_LINE>>>"
  set extra_h($s) { }
}
append mhargs "\$(SRCDIR_extsrc)/sqlite3.h <<<NEXT_LINE>>>"
append mhargs "\$(SRCDIR)/th.h <<<NEXT_LINE>>>"
#append mhargs "\$(SRCDIR_extsrc)/cson_amalgamation.h <<<NEXT_LINE>>>"
append mhargs "\$(OBJDIR)/VERSION.h "
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

writeln "\$(SQLITE3_OBJ):\t\$(SQLITE3_SRC)"
writeln "\t\$(XTCC) \$(SQLITE_OPTIONS) \$(SQLITE_CFLAGS) \$(SEE_FLAGS) \\"
writeln "\t\t-c \$(SQLITE3_SRC) -o \$@"

writeln "\$(OBJDIR)/shell.o:\t\$(SQLITE3_SHELL_SRC) \$(SRCDIR_extsrc)/sqlite3.h"
writeln "\t\$(XTCC) \$(SHELL_OPTIONS) \$(SHELL_CFLAGS) \$(SEE_FLAGS) \$(LINENOISE_DEF.\$(USE_LINENOISE)) -c \$(SQLITE3_SHELL_SRC) -o \$@\n"

writeln "\$(OBJDIR)/linenoise.o:\t\$(SRCDIR_extsrc)/linenoise.c \$(SRCDIR_extsrc)/linenoise.h"
writeln "\t\$(XTCC) -c \$(SRCDIR_extsrc)/linenoise.c -o \$@\n"

writeln "\$(OBJDIR)/th.o:\t\$(SRCDIR)/th.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th.c -o \$@\n"

writeln "\$(OBJDIR)/th_lang.o:\t\$(SRCDIR)/th_lang.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_lang.c -o \$@\n"

writeln "\$(OBJDIR)/th_tcl.o:\t\$(SRCDIR)/th_tcl.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_tcl.c -o \$@\n"

writeln [string map [list <<<NEXT_LINE>>> \\] {
$(OBJDIR)/pikchr.o:	$(SRCDIR_extsrc)/pikchr.c
	$(XTCC) $(PIKCHR_OPTIONS) -c $(SRCDIR_extsrc)/pikchr.c -o $@

$(OBJDIR)/cson_amalgamation.o: $(SRCDIR_extsrc)/cson_amalgamation.c
	$(XTCC) -c $(SRCDIR_extsrc)/cson_amalgamation.c -o $@

$(SRCDIR_extsrc)/pikchr.js: $(SRCDIR_extsrc)/pikchr.c $(MAKEFILE_LIST)
	$(EMCC_WRAPPER) -o $@ $(EMCC_OPT) --no-entry <<<NEXT_LINE>>>
        -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,setValue,getValue,stackSave,stackAlloc,stackRestore <<<NEXT_LINE>>>
        -sEXPORTED_FUNCTIONS=_pikchr,_pikchr_version $(SRCDIR_extsrc)/pikchr.c <<<NEXT_LINE>>>
        -sENVIRONMENT=web <<<NEXT_LINE>>>
        -sMODULARIZE <<<NEXT_LINE>>>
        -sEXPORT_NAME=initPikchrModule <<<NEXT_LINE>>>
        --minify 0
	$(TCLSH) $(TOPDIR)/tools/randomize-js-names.tcl $(SRCDIR_extsrc)
	@chmod -x $(SRCDIR_extsrc)/pikchr.wasm
wasm: $(SRCDIR_extsrc)/pikchr.js

#
# compile_commands.json support...
#
# We have to avoid applying compile_commands support to the in-tree
# tools, as those compile with BCC, which may differ from TCC.
# e.g. BCC might be gcc (which does not support -MJ ...) while TCC is
# clang (which does).
#
# What follows is more verbose than strictly necessary because we're
# limited to POSIX make syntax.
all:
compile-commands-dir.yes = $(OBJDIR)
compile-commands-dir.no =
compile-commands-dir = $(compile-commands-dir.$(MAKE_COMPILATION_DB))
compile-command-args.yes = -MJ $(TOPDIR)/$(compile-commands-dir)/$(@F:.o=.o.json)
compile-command-args.no =
TCCFLAGS += $(compile-command-args.$(MAKE_COMPILATION_DB))
compile_commands.json = $(TOPDIR)/compile_commands.json
# compile_commands.json is a concatenation of the .o.json files
# generated by the compilation process via TCCFLAGS. We have a
# potential race condition in parallel builds, where a .o.json file is
# not yet written to completion before compile_commands.json is
# processed. How to resolve that in a way compatible with POSIX make
# is unclear.
#
# This obscure sed bit ensures that the resulting JSON array does not
# have a trailing comma.
$(compile_commands.json): $(OBJ)
	@-rm -f $@
	@{ echo '['; cat $(compile-commands-dir)/*.o.json | tr '\n' ' ' | sed -e 's/, $$//'; echo ']'; } > $@
	@echo "Generated $@"
compile-commands.no:
compile-commands.yes: $(compile_commands.json)
all: compile-commands.$(MAKE_COMPILATION_DB)
clean: compile-commands-clean
compile-commands-clean:
	rm -fr $(compile_commands.json)

#
# End compile_commands.json support
#

#
# The list of all the targets that do not correspond to real files. This stops
# 'make' from getting confused when someone makes an error in a rule.
#

.PHONY: all install test clean
.PHONY: compile-commands-clean compile-commands-dir
}]

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
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "tools/makemake.tcl")
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
SRCDIR_extsrc = extsrc
SRCDIR_tools = tools

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

#### Enable JSON (https://www.json.org) support using "cson"
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

#### Use POSIX memory APIs from "sys/mman.h"
#
# USE_MMAN_H = 1

#### Use the SQLite Encryption Extension
#
# USE_SEE = 1

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
ZLIBCONFIG =
ZLIBTARGETS =
else
SSLCONFIG = mingw64
ZLIBCONFIG =
ZLIBTARGETS =
endif

#### Disable creation of the OpenSSL shared libraries.  Also, disable support
#    for SSLv3 (i.e. thereby forcing the use of TLS).
#
SSLCONFIG += no-ssl3 no-weak-ssl-ciphers no-shared

#### When using zlib, make sure that OpenSSL is configured to use the zlib
#    that Fossil knows about (i.e. the one within the source tree).
#
SSLCONFIG +=  --with-zlib-lib=$(PWD)/$(ZLIBDIR) --with-zlib-include=$(PWD)/$(ZLIBDIR) zlib

#### The directories where the OpenSSL include and library files are located.
#
OPENSSLDIR = $(SRCDIR)/../compat/openssl
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
TCC += -I$(SRCDIR_extsrc)

#### Add the necessary command line options to build with debugging
#    symbols, if enabled.
#
ifdef FOSSIL_ENABLE_SYMBOLS
TCC += -g
else
TCC += -Os
endif

TCC += -L$(ZLIBDIR) -I$(ZINCDIR)

#### Compile resources for use in building executables that will run
#    on the target platform.
#
RCC = $(PREFIX)windres -I$(SRCDIR) -I$(ZINCDIR)
RCC += -I$(SRCDIR_extsrc)

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

# With "sys/mman.h" support
ifdef USE_MMAN_H
TCC += -DUSE_MMAN_H=1
RCC += -DUSE_MMAN_H=1
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

#### zlib is required.
#
LIB += -lz

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

#### Library required for DNS lookups.
#
LIB += -ldnsapi

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
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.rc) $(subst /,\,$(OBJDIR))
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.ico) $(subst /,\,$(OBJDIR))
	$(CP) $(subst /,\,$(SRCDIR)\..\win\fossil.exe.manifest) $(subst /,\,$(OBJDIR))
else
	$(CP) $(SRCDIR)/../win/fossil.rc $(OBJDIR)
	$(CP) $(SRCDIR)/../win/fossil.ico $(OBJDIR)
	$(CP) $(SRCDIR)/../win/fossil.exe.manifest $(OBJDIR)
endif
	$(RCC) $(OBJDIR)/fossil.rc -o $(OBJDIR)/fossil.o

install:	$(OBJDIR) $(APPNAME)
ifdef USE_WINDOWS
	$(MKDIR) $(subst /,\,$(INSTALLDIR))
	$(CP) $(subst /,\,$(APPNAME)) $(subst /,\,$(INSTALLDIR))
else
	$(MKDIR) $(INSTALLDIR)
	$(CP) $(APPNAME) $(INSTALLDIR)
endif

$(OBJDIR):
ifdef USE_WINDOWS
	$(MKDIR) $(subst /,\,$(OBJDIR))
else
	$(MKDIR) $(OBJDIR)
endif

$(TRANSLATE):	$(SRCDIR_tools)/translate.c
	$(XBCC) -o $@ $(SRCDIR_tools)/translate.c

$(MAKEHEADERS):	$(SRCDIR_tools)/makeheaders.c
	$(XBCC) -o $@ $(SRCDIR_tools)/makeheaders.c

$(MKINDEX):	$(SRCDIR_tools)/mkindex.c
	$(XBCC) -o $@ $(SRCDIR_tools)/mkindex.c

$(MKBUILTIN):	$(SRCDIR_tools)/mkbuiltin.c
	$(XBCC) -o $@ $(SRCDIR_tools)/mkbuiltin.c

$(MKVERSION): $(SRCDIR_tools)/mkversion.c
	$(XBCC) -o $@ $(SRCDIR_tools)/mkversion.c

$(CODECHECK1):	$(SRCDIR_tools)/codecheck1.c
	$(XBCC) -o $@ $(SRCDIR_tools)/codecheck1.c

# WARNING. DANGER. Running the test suite modifies the repository the
# build is done from, i.e. the checkout belongs to. Do not sync/push
# the repository after running the tests.
test:	$(OBJDIR) $(APPNAME)
	$(TCLSH) $(SRCDIR)/../test/tester.tcl $(APPNAME)

$(OBJDIR)/VERSION.h:	$(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(MKVERSION) $(OBJDIR)/phony.h
	$(MKVERSION) $(SRCDIR)/../manifest.uuid $(SRCDIR)/../manifest $(SRCDIR)/../VERSION >$@

$(OBJDIR)/phony.h:
	# Force rebuild of VERSION.h every time "make" is run

# The USE_SYSTEM_SQLITE variable may be undefined, set to 0 or 1.
# If it is set to 1, then there is no need to build or link
# the sqlite3.o object. Instead, the system SQLite will be linked
# using -lsqlite3.
#
# Closely related is SQLITE3_ORIGIN, with the same 0/1 mapping,
# plus a value of 2 means that we are building a client-provided
# sqlite3.c.
SQLITE3_OBJ.0 = $(OBJDIR)/sqlite3.o
SQLITE3_OBJ.1 = $(OBJDIR)/sqlite3-see.o
# SQLITE3_OBJ.2 is set by the configure process
SQLITE3_OBJ.  = $(SQLITE3_OBJ.0)
SQLITE3_OBJ   = $(SQLITE3_OBJ.$(SQLITE3_ORIGIN))

# The USE_SEE variable may be undefined, 0 or 1.  If undefined or 0,
# in-tree SQLite is used.  If 1, then sqlite3-see.c (not part of the
# source tree) is used and extra flags are provided to enable the
# SQLite Encryption Extension.
SQLITE3_SRC.0 = $(SRCDIR_extsrc)/sqlite3.c
SQLITE3_SRC.1 = $(SRCDIR_extsrc)/sqlite3-see.c
# SQLITE3_SRC.2 is set by top-level configure/makefile process.
SQLITE3_SRC. = $(SRCDIR_extsrc)/sqlite3.c
SQLITE3_SRC = $(SQLITE3_SRC.$(SQLITE3_ORIGIN))
SQLITE3_SHELL_SRC.0 = $(SRCDIR_extsrc)/shell.c
SQLITE3_SHELL_SRC.1 = $(SRCDIR_extsrc)/shell-see.c
# SQLITE3_SHELL_SRC.2 comes from the configure process
SQLITE3_SHELL_SRC. = $(SRCDIR_extsrc)/shell.c
SQLITE3_SHELL_SRC = $(SQLITE3_SHELL_SRC.$(SQLITE3_ORIGIN))
SEE_FLAGS.0 =
SEE_FLAGS.1 = -DSQLITE_HAS_CODEC -DSQLITE_SHELL_DBKEY_PROC=fossil_key
SEE_FLAGS. =
SEE_FLAGS = $(SEE_FLAGS.$(USE_SEE))
}

writeln [string map [list <<<NEXT_LINE>>> \\] {
EXTRAOBJ = <<<NEXT_LINE>>>
 $(SQLITE3_OBJ.$(SQLITE3_ORIGIN)) <<<NEXT_LINE>>>
 $(OBJDIR)/pikchr.o <<<NEXT_LINE>>>
 $(OBJDIR)/shell.o <<<NEXT_LINE>>>
 $(OBJDIR)/th.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_lang.o <<<NEXT_LINE>>>
 $(OBJDIR)/th_tcl.o <<<NEXT_LINE>>>
 $(OBJDIR)/cson_amalgamation.o
}]

writeln {
zlib:	$(ZLIBTARGETS)
	$(MAKE) -C $(ZLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) $(ZLIBCONFIG) -f win32/Makefile.gcc libz.a

clean-zlib:
	$(MAKE) -C $(ZLIBDIR) PREFIX=$(PREFIX) CC=$(PREFIX)$(TCCEXE) -f win32/Makefile.gcc clean

BLDTARGETS = zlib

openssl:	$(BLDTARGETS)
	cd $(OPENSSLLIBDIR);./Configure --cross-compile-prefix=$(PREFIX) $(SSLCONFIG)
	sed -i -e 's/^PERL=C:\\.*$$/PERL=perl.exe/i' $(OPENSSLLIBDIR)/Makefile
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

$(APPNAME):	$(APPTARGETS) $(OBJDIR)/headers $(CODECHECK1) $(EXTRAOBJ) $(OBJ) $(OBJDIR)/fossil.o
	$(CODECHECK1) $(TRANS_SRC)
	$(TCC) -o $@ $(EXTRAOBJ) $(OBJ) $(OBJDIR)/fossil.o $(LIB)

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
  if {[string length $mhargs] > 0} {append mhargs " <<<NEXT_LINE>>>"}
  append mhargs "\$(OBJDIR)/${s}_.c:\$(OBJDIR)/$s.h"
  set extra_h($s) { }
}
foreach s [lsort $src_ext] {
  if {[string length $mhargs] > 0} {append mhargs " <<<NEXT_LINE>>>"}
  append mhargs "\$(SRCDIR_extsrc)/${s}.c:\$(OBJDIR)/$s.h"
  set extra_h($s) { }
}
append mhargs " <<<NEXT_LINE>>>\$(SRCDIR_extsrc)/sqlite3.h"
append mhargs " <<<NEXT_LINE>>>\$(SRCDIR)/th.h"
append mhargs " <<<NEXT_LINE>>>\$(OBJDIR)/VERSION.h"
set mhargs [string map [list <<<NEXT_LINE>>> \\\n\t] $mhargs]
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

set MINGW_PIKCHR_OPTIONS $PIKCHR_OPTIONS

set j " \\\n                 "
writeln "SQLITE_OPTIONS = [join $MINGW_SQLITE_OPTIONS $j]\n"
writeln "SHELL_OPTIONS = [join $SHELL_WIN32_OPTIONS $j]\n"
writeln "PIKCHR_OPTIONS = [join $MINGW_PIKCHR_OPTIONS $j]\n"

writeln "\$(SQLITE3_OBJ):\t\$(SQLITE3_SRC) \$(SRCDIR)/../win/Makefile.mingw"
writeln "\t\$(XTCC) \$(SQLITE_OPTIONS) \$(SQLITE_CFLAGS) \$(SEE_FLAGS) \\"
writeln "\t\t-c \$(SQLITE3_SRC) -o \$@\n"

writeln "\$(OBJDIR)/cson_amalgamation.o:\t\$(SRCDIR_extsrc)/cson_amalgamation.c"
writeln "\t\$(XTCC) -c \$(SRCDIR_extsrc)/cson_amalgamation.c -o \$@\n"
writeln "\$(OBJDIR)/json.o \$(OBJDIR)/json_artifact.o \$(OBJDIR)/json_branch.o \$(OBJDIR)/json_config.o \$(OBJDIR)/json_diff.o \$(OBJDIR)/json_dir.o \$(OBJDIR)/jsos_finfo.o \$(OBJDIR)/json_login.o \$(OBJDIR)/json_query.o \$(OBJDIR)/json_report.o \$(OBJDIR)/json_status.o \$(OBJDIR)/json_tag.o \$(OBJDIR)/json_timeline.o \$(OBJDIR)/json_user.o \$(OBJDIR)/json_wiki.o : \$(SRCDIR)/json_detail.h\n"

writeln "\$(OBJDIR)/shell.o:\t\$(SQLITE3_SHELL_SRC) \$(SRCDIR_extsrc)/sqlite3.h \$(SRCDIR)/../win/Makefile.mingw"
writeln "\t\$(XTCC) \$(SHELL_OPTIONS) \$(SHELL_CFLAGS) \$(SEE_FLAGS) -c \$(SQLITE3_SHELL_SRC) -o \$@\n"

writeln "\$(OBJDIR)/th.o:\t\$(SRCDIR)/th.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th.c -o \$@\n"

writeln "\$(OBJDIR)/th_lang.o:\t\$(SRCDIR)/th_lang.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_lang.c -o \$@\n"

writeln "\$(OBJDIR)/th_tcl.o:\t\$(SRCDIR)/th_tcl.c"
writeln "\t\$(XTCC) -c \$(SRCDIR)/th_tcl.c -o \$@\n"

writeln "\$(OBJDIR)/pikchr.o:\t\$(SRCDIR_extsrc)/pikchr.c"
writeln "\t\$(XTCC) \$(PIKCHR_OPTIONS) -c \$(SRCDIR_extsrc)/pikchr.c -o \$@\n"

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
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "tools/makemake.tcl")
##############################################################################
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
B      = ..
SRCDIR = $B\src
SRCDIR_extsrc = $B\extsrc
SRCDIR_tools = $B\tools
OBJDIR = .
O      = .obj
E      = .exe


# Maybe DMDIR, SSL or INCL needs adjustment
DMDIR  = c:\DM
INCL   = -I. -I$(SRCDIR) -I$(SRCDIR_extsrc) -I$B\win\include -I$(DMDIR)\extra\include

#SSL   =  -DFOSSIL_ENABLE_SSL=1
SSL    =

CFLAGS = -o
BCC    = $(DMDIR)\bin\dmc $(CFLAGS)
TCC    = $(DMDIR)\bin\dmc $(CFLAGS) $(DMCDEF) $(SSL) $(INCL)
LIBS   = $(DMDIR)\extra\lib\ zlib wsock32 advapi32 dnsapi
}
writeln "SQLITE_OPTIONS = [join $SQLITE_OPTIONS { }]\n"
writeln "SHELL_OPTIONS = [join $SHELL_WIN32_OPTIONS { }]\n"
writeln "PIKCHR_OPTIONS = [join $PIKCHR_OPTIONS { }]\n"
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
translate$E: $(SRCDIR_tools)\translate.c
	$(BCC) -o$@ $**

makeheaders$E: $(SRCDIR_tools)\makeheaders.c
	$(BCC) -o$@ $**

mkindex$E: $(SRCDIR_tools)\mkindex.c
	$(BCC) -o$@ $**

mkbuiltin$E: $(SRCDIR_tools)\mkbuiltin.c
	$(BCC) -o$@ $**

mkversion$E: $(SRCDIR_tools)\mkversion.c
	$(BCC) -o$@ $**

codecheck1$E: $(SRCDIR_tools)\codecheck1.c
	$(BCC) -o$@ $**

$(OBJDIR)\shell$O : $(SRCDIR_extsrc)\shell.c
	$(TCC) -o$@ -c $(SHELL_OPTIONS) $(SQLITE_OPTIONS) $(SHELL_CFLAGS) $**

$(OBJDIR)\sqlite3$O : $(SRCDIR_extsrc)\sqlite3.c
	$(TCC) -o$@ -c $(SQLITE_OPTIONS) $(SQLITE_CFLAGS) $**

$(OBJDIR)\th$O : $(SRCDIR)\th.c
	$(TCC) -o$@ -c $**

$(OBJDIR)\th_lang$O : $(SRCDIR)\th_lang.c
	$(TCC) -o$@ -c $**

$(OBJDIR)\cson_amalgamation.h : $(SRCDIR_extsrc)\cson_amalgamation.h
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
foreach s [lsort $src_ext] {
  writeln -nonewline "\$(SRCDIR_extsrc)\\${s}.c:$s.h "
}
writeln "\$(SRCDIR_extsrc)\\sqlite3.h \$(SRCDIR)\\th.h VERSION.h \$(SRCDIR_extsrc)\\cson_amalgamation.h"
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
# WARNING: DO NOT EDIT, AUTOMATICALLY GENERATED FILE (SEE "tools/makemake.tcl")
##############################################################################
#
#
# This file is automatically generated.  Instead of editing this
# file, edit "makemake.tcl" then run "tclsh makemake.tcl"
# to regenerate this file.
#
B       = ..
SRCDIR  = $(B)\src
SRCDIR_extsrc = $(B)\extsrc
SRCDIR_tools = $(B)\tools
T       = .
OBJDIR  = $(T)
OX      = $(OBJDIR)
O       = .obj
E       = .exe
P       = .pdb
DBGOPTS = /Od

INSTALLDIR = .
!ifdef DESTDIR
INSTALLDIR = $(DESTDIR)\$(INSTALLDIR)
!endif

# When building out of source, this Makefile needs to know the path to the base
# top-level directory for this project. Pass it on NMAKE command line via make
# variable B:
#   NMAKE /f "path\to\this\Makefile" B="path/to/fossil/root"
#
# NOTE: Make sure B path has no trailing backslash, UNIX-style path is OK too.
#
!if !exist("$(B)\.fossil-settings")
!error Please specify path to project base directory: B="path/to/fossil"
!endif

# Perl is only necessary if OpenSSL support is enabled and it is built from
# source code.  The PERLDIR environment variable, if it exists, should point
# to the directory containing the main Perl executable specified here (i.e.
# "perl.exe").
PERL    = perl.exe

# Enable use of available compiler optimizations?
!ifndef OPTIMIZATIONS
OPTIMIZATIONS = 2
!endif

# Enable debugging symbols?
!ifndef DEBUG
DEBUG = 0
!endif
!ifdef FOSSIL_DEBUG
DEBUG = 1
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
SSLDIR    = $(B)\compat\openssl
SSLINCDIR = $(SSLDIR)\include
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLLIBDIR = $(SSLDIR)
!else
SSLLIBDIR = $(SSLDIR)
!endif
SSLLIB    = libssl.lib libcrypto.lib user32.lib gdi32.lib crypt32.lib
!if "$(PLATFORM)"=="amd64" || "$(PLATFORM)"=="x64"
!message Using 'x64' platform for OpenSSL...
SSLCONFIG = VC-WIN64A no-asm no-ssl3 no-weak-ssl-ciphers
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
!endif
!elseif "$(PLATFORM)"=="ia64"
!message Using 'ia64' platform for OpenSSL...
SSLCONFIG = VC-WIN64I no-asm no-ssl3 no-weak-ssl-ciphers
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
!endif
!else
!message Assuming 'x86' platform for OpenSSL...
SSLCONFIG = VC-WIN32 no-asm no-ssl3 no-weak-ssl-ciphers
!if $(FOSSIL_DYNAMIC_BUILD)!=0
SSLCONFIG = $(SSLCONFIG) shared
!else
SSLCONFIG = $(SSLCONFIG) no-shared
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

INCL      = /I. /I"$(OX)" /I"$(SRCDIR)" /I"$(SRCDIR_extsrc)" /I"$(B)\win\include"

INCL      = $(INCL) /I"$(ZINCDIR)"

!if $(FOSSIL_ENABLE_SSL)!=0
INCL      = $(INCL) /I"$(SSLINCDIR)"
!endif

!if $(FOSSIL_ENABLE_TCL)!=0
INCL      = $(INCL) /I"$(TCLINCDIR)"
!endif

CFLAGS    = /nologo /W2 /WX /utf-8
LDFLAGS   =

CFLAGS    = $(CFLAGS) /D_CRT_SECURE_NO_DEPRECATE /D_CRT_SECURE_NO_WARNINGS
CFLAGS    = $(CFLAGS) /D_CRT_NONSTDC_NO_DEPRECATE /D_CRT_NONSTDC_NO_WARNINGS

!if $(FOSSIL_DYNAMIC_BUILD)!=0
LDFLAGS   = $(LDFLAGS) /MANIFEST
!else
LDFLAGS   = $(LDFLAGS) /NODEFAULTLIB:msvcrt /MANIFEST:NO
!endif

!if $(FOSSIL_ENABLE_WINXP)!=0
XPCFLAGS  = $(XPCFLAGS) /D_WIN32_WINNT=0x0501 /D_USING_V110_SDK71_=1
CFLAGS    = $(CFLAGS) $(XPCFLAGS)
#
# NOTE: For regular builds, /OSVERSION defaults to the /SUBSYSTEM version and
# explicit initialization is redundant, but is required for post-built edits.
#
!if "$(PLATFORM)"=="amd64" || "$(PLATFORM)"=="x64"
XPLDFLAGS = $(XPLDFLAGS) /OSVERSION:5.02 /SUBSYSTEM:CONSOLE,5.02
!else
XPLDFLAGS = $(XPLDFLAGS) /OSVERSION:5.01 /SUBSYSTEM:CONSOLE,5.01
!endif
LDFLAGS   = $(LDFLAGS) $(XPLDFLAGS)
#
# NOTE: Only XPCFLAGS is forwarded to the OpenSSL configuration, and XPLDFLAGS
# is applied in a separate post-build step, see below for more information.
#
!if $(FOSSIL_ENABLE_SSL)!=0
SSLCONFIG = $(SSLCONFIG) $(XPCFLAGS)
!endif
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

!if $(OPTIMIZATIONS)>3
RELOPTS = /Os
!elseif $(OPTIMIZATIONS)>2
RELOPTS = /Ox
!elseif $(OPTIMIZATIONS)>1
RELOPTS = /O2
!elseif $(OPTIMIZATIONS)>0
RELOPTS = /O1
!else
RELOPTS =
!endif

!if $(DEBUG)!=0
CFLAGS    = $(CFLAGS) /Zi $(CRTFLAGS) $(DBGOPTS) /DFOSSIL_DEBUG /DTH_MEMDEBUG
LDFLAGS   = $(LDFLAGS) /DEBUG
!else
CFLAGS    = $(CFLAGS) $(CRTFLAGS) $(RELOPTS)
!endif

BCC       = $(CC) $(CFLAGS)
TCC       = $(CC) /c $(CFLAGS) $(MSCDEF) $(INCL)
RCC       = $(RC) /D_WIN32 /D_MSC_VER $(MSCDEF) $(INCL)
MTC       = mt
LIBS      = ws2_32.lib advapi32.lib dnsapi.lib
LIBDIR    =

!if $(FOSSIL_DYNAMIC_BUILD)!=0
TCC       = $(TCC) /DFOSSIL_DYNAMIC_BUILD=1
RCC       = $(RCC) /DFOSSIL_DYNAMIC_BUILD=1
!endif

LIBS      = $(LIBS) $(ZLIB)
LIBDIR    = $(LIBDIR) /LIBPATH:"$(ZLIBDIR)"

!if $(FOSSIL_ENABLE_JSON)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_JSON=1
RCC       = $(RCC) /DFOSSIL_ENABLE_JSON=1
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_SSL=1
RCC       = $(RCC) /DFOSSIL_ENABLE_SSL=1
LIBS      = $(LIBS) $(SSLLIB)
LIBDIR    = $(LIBDIR) /LIBPATH:"$(SSLLIBDIR)"
!endif

!if $(FOSSIL_ENABLE_EXEC_REL_PATHS)!=0
TCC       = $(TCC) /DFOSSIL_ENABLE_EXEC_REL_PATHS=1
RCC       = $(RCC) /DFOSSIL_ENABLE_EXEC_REL_PATHS=1
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

regsub -all {[-]D} [join $PIKCHR_OPTIONS { }] {/D} MSC_PIKCHR_OPTIONS
set j " \\\n                "
writeln "PIKCHR_OPTIONS = [join $MSC_PIKCHR_OPTIONS $j]\n"

writeln -nonewline "SRC   = "
set i 0
foreach s [lsort $src] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  writeln -nonewline "\"\$(OX)\\${s}_.c\""; incr i
}
foreach s [lsort $src_ext] {
  writeln " \\"
  writeln -nonewline "        "
  writeln -nonewline "\"\$(SRCDIR_extsrc)\\${s}.c\""; incr i
}
writeln "\n"
writeln -nonewline "EXTRA_FILES   = "
set i 0
foreach s [lsort $extra_files] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  set s [regsub -all / $s \\]
  writeln -nonewline "\"\$(SRCDIR)\\${s}\""; incr i
}
writeln "\n"
set AdditionalObj [list shell sqlite3 th th_lang th_tcl cson_amalgamation pikchr]
writeln -nonewline "OBJ   = "
set i 0
foreach s [lsort [concat $src $AdditionalObj]] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "        "
  }
  writeln -nonewline "\"\$(OX)\\$s\$O\""; incr i
}
if {$i > 0} {
  writeln " \\"
}
writeln -nonewline "        \"\$(OX)\\fossil.res\"\n\n"
writeln [string map [list <<<NEXT_LINE>>> \\] {
!ifndef BASEAPPNAME
BASEAPPNAME = fossil
!endif

APPNAME     = $(OX)\$(BASEAPPNAME)$(E)
PDBNAME     = $(OX)\$(BASEAPPNAME)$(P)
APPTARGETS  =

all: "$(OX)" "$(APPNAME)"

$(BASEAPPNAME): "$(APPNAME)"

$(BASEAPPNAME)$(E): "$(APPNAME)"

install: "$(APPNAME)"
	echo F | xcopy /Y "$(APPNAME)" "$(INSTALLDIR)"\*
!if $(DEBUG)!=0
	echo F | xcopy /Y "$(PDBNAME)" "$(INSTALLDIR)"\*
!endif

$(OX):
	@-mkdir $@

zlib:
	@echo Building zlib from "$(ZLIBDIR)"...
!if $(FOSSIL_ENABLE_WINXP)!=0
	@pushd "$(ZLIBDIR)" && $(MAKE) /f win32\Makefile.msc $(ZLIB) "CC=cl $(XPCFLAGS)" "LD=link $(XPLDFLAGS)" && popd
!else
	@pushd "$(ZLIBDIR)" && $(MAKE) /f win32\Makefile.msc $(ZLIB) && popd
!endif

clean-zlib:
	@pushd "$(ZLIBDIR)" && $(MAKE) /f win32\Makefile.msc clean && popd

!if $(FOSSIL_ENABLE_SSL)!=0
openssl:
	@echo Building OpenSSL from "$(SSLDIR)"...
!if $(FOSSIL_ENABLE_WINXP)!=0
	@echo Passing XPCFLAGS = [ $(XPCFLAGS) ] to the OpenSSL configuration...
!endif
!ifdef PERLDIR
	@pushd "$(SSLDIR)" && "$(PERLDIR)\$(PERL)" Configure $(SSLCONFIG) && popd
!else
	@pushd "$(SSLDIR)" && "$(PERL)" Configure $(SSLCONFIG) && popd
!endif
	@pushd "$(SSLDIR)" && $(MAKE) && popd
!if $(FOSSIL_ENABLE_WINXP)!=0 && $(FOSSIL_DYNAMIC_BUILD)!=0
#
# NOTE: Appending custom linker flags to the OpenSSL default linker flags is
# somewhat difficult, as summarized in this Fossil Forum post:
#
#   https://fossil-scm.org/forum/forumpost/a9a2d6af28b
#
# Therefore the custom linker flags required for Windows XP dynamic builds are
# applied in a separate post-build step.
#
# If the build stops here, or if the custom linker flags are outside the scope
# of `editbin` or `link /EDIT` (i.e. additional libraries), consider tweaking
# the OpenSSL makefile by hand.
#
# Also note that this step changes the subsystem for the OpenSSL DLLs from
# WINDOWS to CONSOLE, but which has no effect on DLLs.
#
	@echo Applying XPLDFLAGS = [ $(XPLDFLAGS) ] to the OpenSSL DLLs...
	@for /F "usebackq delims=" %F in (`dir /A:-D/B "$(SSLDIR)\*.dll" 2^>nul`) <<<NEXT_LINE>>>
		do @( <<<NEXT_LINE>>>
			echo %F & <<<NEXT_LINE>>>
			link /EDIT /NOLOGO $(XPLDFLAGS) "$(SSLDIR)\%F" || exit 1 <<<NEXT_LINE>>>
		)
!endif

clean-openssl:
	@pushd "$(SSLDIR)" && $(MAKE) clean && popd
!endif

!if $(FOSSIL_BUILD_ZLIB)!=0
APPTARGETS = $(APPTARGETS) zlib
!endif

!if $(FOSSIL_ENABLE_SSL)!=0
!if $(FOSSIL_BUILD_SSL)!=0
APPTARGETS = $(APPTARGETS) openssl
!endif
!endif

"$(APPNAME)" : $(APPTARGETS) "$(OBJDIR)\translate$E" "$(OBJDIR)\mkindex$E" "$(OBJDIR)\codecheck1$E" "$(OX)\headers" $(OBJ) "$(OX)\linkopts"
	"$(OBJDIR)\codecheck1$E" $(SRC)
	link $(LDFLAGS) /OUT:$@ /PDB:$(@D)\ $(LIBDIR) Wsetargv.obj "$(OX)\fossil.res" @"$(OX)\linkopts"
	if exist "$(B)\win\fossil.exe.manifest" <<<NEXT_LINE>>>
		$(MTC) -nologo -manifest "$(B)\win\fossil.exe.manifest" -outputresource:$@;1

"$(OX)\linkopts": "$(B)\win\Makefile.msc"}]
set redir {>}
foreach s [lsort [concat $src $AdditionalObj]] {
  writeln "\techo \"\$(OX)\\$s.obj\" $redir \$@"
  set redir {>>}
}
set redir {>>}
writeln "\techo \$(LIBS) $redir \$@"
writeln {
"$(OBJDIR)\translate$E": "$(SRCDIR_tools)\translate.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

"$(OBJDIR)\makeheaders$E": "$(SRCDIR_tools)\makeheaders.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

"$(OBJDIR)\mkindex$E": "$(SRCDIR_tools)\mkindex.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

"$(OBJDIR)\mkbuiltin$E": "$(SRCDIR_tools)\mkbuiltin.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

"$(OBJDIR)\mkversion$E": "$(SRCDIR_tools)\mkversion.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

"$(OBJDIR)\codecheck1$E": "$(SRCDIR_tools)\codecheck1.c"
	$(BCC) /Fe$@ /Fo$(@D)\ /Fd$(@D)\ $**

!if $(USE_SEE)!=0
SEE_FLAGS = /DSQLITE_HAS_CODEC=1 /DSQLITE_SHELL_DBKEY_PROC=fossil_key
SQLITE3_SHELL_SRC = $(SRCDIR)\shell-see.c
SQLITE3_SRC = $(SRCDIR_extsrc)\sqlite3-see.c
!else
SEE_FLAGS =
SQLITE3_SHELL_SRC = $(SRCDIR_extsrc)\shell.c
SQLITE3_SRC = $(SRCDIR_extsrc)\sqlite3.c
!endif

"$(OX)\shell$O" : "$(SQLITE3_SHELL_SRC)" "$(B)\win\Makefile.msc"
	$(TCC) /Fo$@ /Fd$(@D)\ $(SHELL_OPTIONS) $(SQLITE_OPTIONS) $(SHELL_CFLAGS) $(SEE_FLAGS) -c "$(SQLITE3_SHELL_SRC)"

"$(OX)\sqlite3$O" : "$(SQLITE3_SRC)" "$(B)\win\Makefile.msc"
	$(TCC) /Fo$@ /Fd$(@D)\ -c $(SQLITE_OPTIONS) $(SQLITE_CFLAGS) $(SEE_FLAGS) "$(SQLITE3_SRC)"

"$(OX)\th$O" : "$(SRCDIR)\th.c"
	$(TCC) /Fo$@ /Fd$(@D)\ -c $**

"$(OX)\th_lang$O" : "$(SRCDIR)\th_lang.c"
	$(TCC) /Fo$@ /Fd$(@D)\ -c $**

"$(OX)\th_tcl$O" : "$(SRCDIR)\th_tcl.c"
	$(TCC) /Fo$@ /Fd$(@D)\ -c $**

"$(OX)\pikchr$O" : "$(SRCDIR_extsrc)\pikchr.c"
	$(TCC) $(PIKCHR_OPTIONS) /Fo$@ /Fd$(@D)\ -c $**

"$(OX)\VERSION.h" : "$(OBJDIR)\mkversion$E" "$(B)\manifest.uuid" "$(B)\manifest" "$(B)\VERSION" "$(B)\phony.h"
	"$(OBJDIR)\mkversion$E" "$(B)\manifest.uuid" "$(B)\manifest" "$(B)\VERSION" > $@

"$(B)\phony.h" :
	rem Force rebuild of VERSION.h whenever nmake is run

"$(OX)\cson_amalgamation$O" : "$(SRCDIR_extsrc)\cson_amalgamation.c"
	$(TCC) /Fo$@ /Fd$(@D)\ -c $**

"$(OX)\page_index.h": "$(OBJDIR)\mkindex$E" $(SRC)
	$** > $@

"$(OX)\builtin_data.h":	"$(OBJDIR)\mkbuiltin$E" "$(OX)\builtin_data.reslist"
	"$(OBJDIR)\mkbuiltin$E" --prefix "$(SRCDIR)/" --reslist "$(OX)\builtin_data.reslist" > $@

cleanx:
	-del "$(OX)\*.obj" 2>NUL
	-del "$(OBJDIR)\*.obj" 2>NUL
	-del "$(OX)\*_.c" 2>NUL
	-del "$(OX)\*.h" 2>NUL
	-del "$(OX)\*.ilk" 2>NUL
	-del "$(OX)\*.map" 2>NUL
	-del "$(OX)\*.res" 2>NUL
	-del "$(OX)\*.reslist" 2>NUL
	-del "$(OX)\headers" 2>NUL
	-del "$(OX)\linkopts" 2>NUL
	-del "$(OX)\vc*.pdb" 2>NUL

clean: cleanx
	-del "$(APPNAME)" 2>NUL
	-del "$(PDBNAME)" 2>NUL
	-del "$(OBJDIR)\translate$E" 2>NUL
	-del "$(OBJDIR)\translate$P" 2>NUL
	-del "$(OBJDIR)\mkindex$E" 2>NUL
	-del "$(OBJDIR)\mkindex$P" 2>NUL
	-del "$(OBJDIR)\makeheaders$E" 2>NUL
	-del "$(OBJDIR)\makeheaders$P" 2>NUL
	-del "$(OBJDIR)\mkversion$E" 2>NUL
	-del "$(OBJDIR)\mkversion$P" 2>NUL
	-del "$(OBJDIR)\mkcss$E" 2>NUL
	-del "$(OBJDIR)\mkcss$P" 2>NUL
	-del "$(OBJDIR)\codecheck1$E" 2>NUL
	-del "$(OBJDIR)\codecheck1$P" 2>NUL
	-del "$(OBJDIR)\mkbuiltin$E" 2>NUL
	-del "$(OBJDIR)\mkbuiltin$P" 2>NUL

realclean: clean

"$(OBJDIR)\json$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_artifact$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_branch$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_config$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_diff$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_dir$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_finfo$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_login$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_query$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_report$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_status$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_tag$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_timeline$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_user$O" : "$(SRCDIR)\json_detail.h"
"$(OBJDIR)\json_wiki$O" : "$(SRCDIR)\json_detail.h"
}

writeln {"$(OX)\builtin_data.reslist": $(EXTRA_FILES) "$(B)\win\Makefile.msc"}
set redir {>}
foreach s [lsort $extra_files] {
  writeln "\techo \"\$(SRCDIR)\\${s}\" $redir \$@"
  set redir {>>}
}

foreach s [lsort $src] {
  set extra_h($s) {}
}
set extra_h(builtin) " \"\$(OX)\\builtin_data.h\""
set extra_h(dispatch) " \"\$(OX)\\page_index.h\""

writeln ""
foreach s [lsort $src] {
  writeln "\"\$(OX)\\$s\$O\" : \"\$(OX)\\${s}_.c\" \"\$(OX)\\${s}.h\"$extra_h($s)"
  writeln "\t\$(TCC) /Fo\$@ /Fd\$(@D)\\ -c \"\$(OX)\\${s}_.c\"\n"
  writeln "\"\$(OX)\\${s}_.c\" : \"\$(SRCDIR)\\$s.c\""
  writeln "\t\"\$(OBJDIR)\\translate\$E\" \$** > \$@\n"
}

writeln "\"\$(OX)\\fossil.res\" : \"\$(B)\\win\\fossil.rc\""
writeln "\t\$(RCC) /fo \$@ \$**\n"

writeln "\"\$(OX)\\headers\": \"\$(OBJDIR)\\makeheaders\$E\" \"\$(OX)\\page_index.h\" \"\$(OX)\\builtin_data.h\" \"\$(OX)\\VERSION.h\""
writeln -nonewline "\t\"\$(OBJDIR)\\makeheaders\$E\" "
set i 0
foreach s [lsort $src] {
  if {$i > 0} {
    writeln " \\"
    writeln -nonewline "\t\t\t"
  }
  writeln -nonewline "\"\$(OX)\\${s}_.c\":\"\$(OX)\\$s.h\""; incr i
}
foreach s [lsort $src_ext] {
  writeln " \\"
  writeln -nonewline "\t\t\t"
  writeln -nonewline "\"\$(SRCDIR_extsrc)\\${s}.c\":\"\$(OX)\\$s.h\""; incr i
}
writeln " \\\n\t\t\t\"\$(SRCDIR_extsrc)\\sqlite3.h\" \\"
writeln "\t\t\t\"\$(SRCDIR)\\th.h\" \\"
writeln "\t\t\t\"\$(OX)\\VERSION.h\" \\"
writeln "\t\t\t\"\$(SRCDIR_extsrc)\\cson_amalgamation.h\""
writeln "\t@copy /Y nul: $@"


close $output_file
#
# End of the win/Makefile.msc output
##############################################################################
##############################################################################
