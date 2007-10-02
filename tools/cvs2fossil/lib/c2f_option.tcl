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
package require snit                            ; # OO system

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
    ## Internal methods and state

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

    proc Validate {} {
	return
    }

    proc Usage {{text {}}} {
	global argv0
	if {$text ne ""} {set text \n$text}
	#trouble fatal "Usage: $argv0 ?option ?value?...? cvs-repository-path$text"
	puts "Usage: $argv0 ?option ?value?...? cvs-repository-path$text"
	exit 1
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::option 1.0
return
