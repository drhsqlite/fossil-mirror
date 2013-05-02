# @synopsis:
#
# The 'cc-run' module provides a way to compile and run a bit of
# code.  This is not cross compilation friendly and is therefore
# against autosetup's general philosophy, but is sometimes the only
# way to perform a test.
#

use cc

module-options {}

# cc-run is based on cctest in cc.tcl.

# @cc-run ?settings?
# 
# Low level C/C++ program checker. Compiles, links, and runs a small
# C/C++ program according to the arguments and returns 1 if OK, or 0
# if not.
#
# Supported settings are:
#
## -cflags cflags      A list of flags to pass to the compiler
## -includes list      A list of includes, e.g. {stdlib.h stdio.h}
## -declare code       Code to declare before main()
## -lang c|c++         Use the C (default) or C++ compiler
## -libs liblist       List of libraries to link, e.g. {-ldl -lm}
## -code code          Code to compile in the body of main()
## -source code        Compile a complete program. Ignore -includes, -declare and -code
## -sourcefile file    Shorthand for -source [readfile [get-define srcdir]/$file]
#
# Unless -source or -sourcefile is specified, the C program looks like:
#
## #include <firstinclude>   /* same for remaining includes in the list */
##
## declare-code              /* any code in -declare, verbatim */
##
## int main(void) {
##   code                    /* any code in -code, verbatim */
##   return 0;
## }
#
# Any failures are recorded in 'config.log'
#
proc cc-run {args} {
	set src conftest__.c
	set tmp conftest__

	# Easiest way to merge in the settings
	cc-with $args {
		array set opts [cc-get-settings]
	}

	if {[info exists opts(-sourcefile)]} {
		set opts(-source) [readfile [get-define srcdir]/$opts(-sourcefile) "#error can't find $opts(-sourcefile)"]
	}
	if {[info exists opts(-source)]} {
		set lines $opts(-source)
	} else {
		foreach i $opts(-includes) {
			if {$opts(-code) ne "" && ![feature-checked $i]} {
				# Compiling real code with an unchecked header file
				# Quickly (and silently) check for it now

				# Remove all -includes from settings before checking
				set saveopts [cc-update-settings -includes {}]
				msg-quiet cc-check-includes $i
				cc-store-settings $saveopts
			}
			if {$opts(-code) eq "" || [have-feature $i]} {
				lappend source "#include <$i>"
			}
		}
		lappend source {*}$opts(-declare)
		lappend source "int main(void) {"
		lappend source $opts(-code)
		lappend source "return 0;"
		lappend source "}"

		set lines [join $source \n]
	}

	# Build the command line
	set cmdline {}
	lappend cmdline {*}[get-define CCACHE]
	switch -exact -- $opts(-lang) {
		c++ {
			lappend cmdline {*}[get-define CXX] {*}[get-define CXXFLAGS]
		}
		c {
			lappend cmdline {*}[get-define CC] {*}[get-define CFLAGS]
		}
		default {
			autosetup-error "cc-run called with unknown language: $opts(-lang)"
		}
	}

	lappend cmdline {*}$opts(-cflags)

	switch -glob -- [get-define host] {
		*-*-darwin* {
			# Don't generate .dSYM directories
			lappend cmdline -gstabs
		}
	}
	lappend cmdline $src -o $tmp {*}$opts(-libs)

	# At this point we have the complete command line and the
	# complete source to be compiled. Get the result from cache if
	# we can
	if {[info exists ::cc_cache($cmdline,$lines)]} {
		msg-checking "(cached) "
		set ok $::cc_cache($cmdline,$lines)
		if {$::autosetup(debug)} {
			configlog "From cache (ok=$ok): [join $cmdline]"
			configlog "============"
			configlog $lines
			configlog "============"
		}
		return $ok
	}

	writefile $src $lines\n

	set ok 1
	if {[catch {exec-with-stderr {*}$cmdline} result errinfo]} {
		configlog "Failed: [join $cmdline]"
		configlog $result
		configlog "============"
		configlog "The failed code was:"
		configlog $lines
		configlog "============"
		set ok 0
	} else {
		if {$::autosetup(debug)} {
			configlog "Compiled OK: [join $cmdline]"
			configlog "============"
			configlog $lines
			configlog "============"
		}
		if {[catch {exec-with-stderr ./$tmp} result errinfo]} {
			configlog "Failed: $tmp"
			configlog $result
			configlog "============"
			configlog "The failed code was:"
			configlog $lines
			configlog "============"
			set ok 0
		} else {
			if {$::autosetup(debug)} {
				configlog "Ran OK: $tmp"
				configlog "============"
				configlog $lines
				configlog "============"
			}
		}
	}
	file delete $src
	file delete $tmp

	# cache it
	set ::cc_cache($cmdline,$lines) $ok

	return $ok
}
