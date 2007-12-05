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

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::tools::misc                       ; # Misc. path reformatting.
package require vc::fossil::import::cvs::fossil       ; # Fossil repository access
package require vc::fossil::import::cvs::pass         ; # Pass management
package require vc::fossil::import::cvs::pass::collar ; # Pass I.
package require vc::fossil::import::cvs::repository   ; # Repository management
package require vc::fossil::import::cvs::state        ; # State storage
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols
package require vc::fossil::import::cvs::cyclebreaker ; # Breaking dependency cycles.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::option {
    # # ## ### ##### ######## #############
    ## Public API, Options.

    # --help, --help-passes, -h
    # --version
    # -p, --pass, --passes
    # --ignore-conflicting-attics
    # --project
    # -v, --verbose
    # -q, --quiet
    # --state (conversion status, ala config.cache)
    # --trunk-only
    # --exclude, --force-tag, --force-branch
    # --batch
    # --fossil PATH

    # -o, --output
    # --dry-run
    # --symbol-transform RE:XX

    # # ## ### ##### ######## #############
    ## Public API, Methods

    typemethod process {arguments} {
	# Syntax of arguments: ?option ?value?...? /path/to/cvs/repository

	while {[IsOption arguments -> option]} {
	    switch -exact -- $option {
		-h                          -
		--help                      { PrintHelp    ; exit 0 }
		--help-passes               { pass help    ; exit 0 }
		--version                   { PrintVersion ; exit 0 }
		-p                          -
		--pass                      -
		--passes                    { pass select [Value arguments] }
		--ignore-conflicting-attics { collar ignore_conflicting_attics }
		--project                   { repository add [Value arguments] }
		-v                          -
		--verbose                   { log verbose }
		-q                          -
		--quiet                     { log quiet }
		--state                     { state usedb [Value arguments] }
		--trunk-only                { repository trunkonly! }
		--exclude                   { project::sym exclude     [Value arguments] }
		--force-tag                 { project::sym forcetag    [Value arguments] }
		--force-branch              { project::sym forcebranch [Value arguments] }
		--batch                     { log noprogress }
		--dots                      { cyclebreaker dotsto [Value arguments] }
		--watch                     { cyclebreaker watch  [Value arguments] }
		--statesavequeriesto        { state savequeriesto [Value arguments] }
		--fossil                    { fossil setlocation  [Value arguments] }
		default {
		    Usage $badoption$option\n$gethelp
		}
	    }
	}

	if {[llength $arguments] > 1} Usage
	if {[llength $arguments] < 1} { Usage $nocvs }
	repository base [striptrailingslash [lindex $arguments 0]]

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
	trouble info "    -v, --verbose Increase application's verbosity"
	trouble info "    -q, --quiet   Decrease application's verbosity"
	trouble info "    --batch       Disable the progress feedback standard to"
	trouble info "                  interactive use."
	trouble info ""
	trouble info "  Conversion control options"
	trouble info ""
	trouble info "    -p, --pass PASS            Run only the specified conversion pass"
	trouble info "    -p, --passes ?START?:?END? Run only the passes START through END,"
	trouble info "                               inclusive."
	trouble info ""
	trouble info "                               Passes are specified by name."
	trouble info ""
	trouble info "    --ignore-conflicting-attics"
	trouble info "                               Prevent abort when conflicting archives"
	trouble info "                               were found in both regular and Attic."
	trouble info ""
	trouble info "    --state PATH               Save state to the specified file, and"
	trouble info "                               load state of previous runs from it too."
	trouble info ""
	trouble info "    --exclude ?PROJECT:?SYMBOL Exclude the named symbol from all or"
	trouble info "                               just the specified project. Both project"
	trouble info "                               and symbol names are glob patterns."
	trouble info ""
	trouble info "    --force-tag ?PROJECT:?SYMBOL"
	trouble info "                               Force the named symbol from all or just"
	trouble info "                               the specified project to be converted as"
	trouble info "                               tag. Both project and symbol names are"
	trouble info "                               glob patterns."
	trouble info ""
	trouble info "    --force-branch ?PROJECT:?SYMBOL"
	trouble info "                               Force the named symbol from all or just"
	trouble info "                               the specified project to be converted as"
	trouble info "                               branch. Both project and symbol names"
	trouble info "                               are glob patterns."
	trouble info ""
	trouble info "  Output control options"
	trouble info ""
	trouble info "    --fossil PATH              Specify where to find the fossil execu-"
	trouble info "                               table if cv2fossil could not find it in"
	trouble info "                               the PATH."
	trouble info ""
	trouble info "  Debug options"
	trouble info ""
	trouble info "    --dots PATH                Write the changeset graphs before, after,"
	trouble info "                               and during breaking the of cycles to the"
	trouble info "                               direcotry PATH, using GraphViz's dot format"
	trouble info ""

	# --project, --cache
	# ...
	return
    }

    proc PrintVersion {} {
	global argv0
	set v [package require vc::fossil::import::cvs]
	trouble info "$argv0 v$v"
	return
    }

    proc Usage {{text {}}} {
	global argv0
	trouble fatal "Usage: $argv0 $usage"
	if {$text ne ""} { trouble fatal "$text" }
	exit 1
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
	# Prevent in-depth validation if the options were already bad.
	trouble abort?

	fossil     validate
	repository validate
	state      setup

	trouble abort?
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export option
    namespace eval option {
	namespace import ::vc::tools::misc::striptrailingslash
	namespace import ::vc::fossil::import::cvs::fossil
	namespace import ::vc::fossil::import::cvs::pass
	namespace import ::vc::fossil::import::cvs::pass::collar
	namespace import ::vc::fossil::import::cvs::cyclebreaker
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::option 1.0
return
