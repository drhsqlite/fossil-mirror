#!/bin/sh
# -*- tcl -*- \
exec tclsh "$0" ${1+"$@"}

# -----------------------------------------------------------------------------

# Import the trunk of a CVS repository wholesale into a fossil repository.

# Limitations implicitly mentioned:
# - No incremental import.
# - No import of branches.

# WIBNI features (beyond eliminating the limitations):
# - Restrict import to specific directory subtrees (SF projects use
#   one repository for several independent modules. Examples: tcllib
#   -> tcllib, tklib, tclapps, etc.). The restriction would allow import
#   of only a specific module.
# - Related to the previous, strip elements from the base path to keep
#   it short.
# - Export to CVS, trunk, possibly branches. I.e. extend the system to be
#   a full bridge. Either Fossil or CVS could be the master repository.

# HACKS. I.e. I do not know if the 'fixes' I use are the correct way
#        of handling the encountered situations.
#
# - File F has archives F,v and Attic/F,v. Currently I will ignore the
#   file in the Attic.
#   Examples: sqlite/os_unix.h
#
# - A specific revision of a file F cannot be checked out (reported
#   error is 'invalid change text'). This indicates a corrupt RCS
#   file, one or more delta are bad. We report but ignore the problem
#   in a best-effort attempt at getting as much history as possible.
#   Examples: tcllib/tklib/modules/tkpiechart/pie.tcl

# -----------------------------------------------------------------------------
# Make private packages accessible.

lappend auto_path [file join [file dirname [info script]] lib]

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log          ; # User Feedback
package require vc::fossil::import::cvs ; # Importer Control
package require vc::cvs::ws             ; # CVS frontend

namespace eval ::import {
    namespace import ::vc::fossil::import::cvs::*
}

# -----------------------------------------------------------------------------

proc main {} {
    commandline -> cvs  fossil
    import::run   $cvs $fossil
    return
}

# -----------------------------------------------------------------------------

proc commandline {__ cv fv} {
    global argv
    upvar 1 $cv cvs $fv fossil

    set verbosity 0

    clinit
    while {[string match "-*" [set opt [this]]]} {
	switch -exact -- $opt {
	    --breakat     { next ; import::configure -breakat   [this] }
	    --cache-rcs   { next ; import::configure -cache-rcs [this] }
	    --nosign      {        import::configure -nosign         1 }
	    --project     { next ; import::configure -project   [this] }
	    --saveto      { next ; import::configure -saveto    [file normalize [this]] }
	    -v            { incr verbosity ; ::vc::tools::log::verbosity $verbosity }
	    -h            -
	    default       usage
	}
	next
    }

    remainder
    if {[llength $argv] != 2} usage
    foreach {cvs fossil} $argv break

    if {![::vc::cvs::ws::check $cvs msg]} {
	usage $msg
    } elseif {[file exists $fossil]} {
	usage "Fossil destination repository exists already."
    }

    return
}

proc this {} {
    global argv
    upvar 1 at at
    return [lindex $argv $at]
}

proc next {} {
    upvar 1 at at
    incr at
    return
}

proc remainder {} {
    upvar 1 at at
    global argv
    set argv [lrange $argv $at end]
    return
}

proc clinit {} {
    upvar 1 at at
    set at 0
    return
}

proc usage {{text {}}} {
    global argv0
    puts stderr "Usage: $argv0 ?-v? ?--nosign? ?--breakat id? ?--saveto path? cvs-repository fossil-repository"
    if {$text eq ""} {
	puts stderr "       --nosign:    Do not sign the imported changesets."
	puts stderr "       --breakat:   Stop just before committing the identified changeset."
	puts stderr "       --cache-rcs: Boolean. Activate caching of rcs parse results"
	puts stderr "       --project:   Path in the CVS repository to limit the import to."
	puts stderr "       --saveto:    Save commit command to the specified file."
	puts stderr "       -v:          Increase log verbosity. Can be used multiple times."
    } else {
	puts stderr "       $text"
    }
    exit
}

# -----------------------------------------------------------------------------

main
exit
