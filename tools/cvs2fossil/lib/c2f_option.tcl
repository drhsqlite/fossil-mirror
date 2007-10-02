## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Option database, processes the command line. Note that not all of
## the option information is stored here. Parts are propagated to
## other pieces of the system and handled there, via option
## delegation

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                         ; # Required runtime.
package require snit                            ; # OO system.
package require vc::tools::trouble              ; # Error reporting.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::option {
    # # ## ### ##### ######## #############
    ## Public API, Options.

    # --help, --help-passes, -h
    # --version
    # --project
    # --cache (conversion status, ala config.cache)

    # -o, --output
    # --dry-run
    # --trunk-only
    # --force-branch RE
    # --force-tag RE
    # --symbol-transform RE:XX
    # --exclude
    # -p, --passes
    # -v, --verbose
    # -q, --quiet

    # # ## ### ##### ######## #############
    ## Public API, Methods

    typemethod process {arguments} {
	# Syntax of arguments: ?option ?value?...? /path/to/cvs/repository

	while {[IsOption arguments -> option]} {
	    switch -exact -- $option {
		-h            -
		--help        PrintHelp
		--help-passes PrintHelpPasses
		--version     PrintVersion
		--project     {
		    #cvs::repository addproject [Value arguments]
		}
		--cache       {
		    # [Value arguments]
		}
		default {
		    Usage $badoption$option\n$gethelp
		}
	    }
	}

	if {[llength $arguments] > 1} Usage
	if {[llength $arguments] < 1} { Usage $nocvs }
	#cvs::repository setbase [lindex $arguments 0]

	Validate
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods, printing information.

    proc PrintHelp {} {
	global argv0
	trouble info "Usage: $argv0 $usage"
	trouble info ""
	trouble info "  Information options"
	trouble info ""
	trouble info "    -h, --help    Print this message and exit with success"
	trouble info "    --help-passes Print list of passes and exit with success"
	trouble info "    --version     Print version number of $argv0"
	trouble info ""
	# --project, --cache
	# ...
	exit 0
    }

    proc PrintHelpPasses {} {
	trouble info ""
	trouble info "Conversion passes:"
	trouble info ""
	set n 0
	foreach {p desc} {
	    CollectAr  {Collect archives}
	    CollectRev {Collect revisions}
	} { trouble info "  [format %2d $n]: $p $desc" ; incr n }
	trouble info ""
	exit 0
    }

    proc PrintVersion {} {
	global argv0
	set v [package require vc::fossil::import::cvs]
	trouble info "$argv0 v$v"
	exit 0
    }

    proc Usage {{text {}}} {
	global argv0
	if {$text ne ""} {set text \n$text}
	trouble fatal "Usage: $argv0 $usage$text"
	# Not reached
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods, command line processing

    typevariable usage     "?option ?value?...? cvs-repository-path"
    typevariable nocvs     "       The cvs-repository-path is missing."
    typevariable badoption "       Bad option "
    typevariable gethelp   "       Use --help to get help."

    proc IsOption {av _ ov} {
	upvar 1 $av arguments $ov option
	set candidate [lindex $arguments 0]
	if {![string match -* $candidate]} {return 0}
	set option    $candidate
	set arguments [lrange $arguments 1 end]
	return 1
    }

    proc Value {av} {
	upvar 1 $av arguments
	set v         [lindex $arguments 0]
	set arguments [lrange $arguments 1 end]
	return $v
    }

    # # ## ### ##### ######## #############
    ## Internal methods, state validation

    proc Validate {} {
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::option {
    namespace import ::vc::tools::trouble
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::option 1.0
return
