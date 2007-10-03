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

## Pass I. This pass scans the repository to import for RCS archives,
## and sorts and filters them into the declared projects, if any
## Without declared projects the whole repository is treated as a
## single project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require fileutil::traverse                  ; # Directory traversal.
package require fileutil                            ; # File & path utilities.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::log                      ; # User feedback.
package require vc::fossil::import::cvs::pass       ; # Pass management.
package require vc::fossil::import::cvs::repository ; # Repository management.

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    CollectAr \
    {Collect archives in repository} \
    ::vc::fossil::import::cvs::pass::collar

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::collar {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# TODO ... artifact/cache - drop projects/files, create projects/files
    }

    typemethod run {} {
	foreach project [repository projects] {
	    set base [$project base]
	    log write 1 collar "Scan $base"

	    set traverse [fileutil::traverse %AUTO% $base]
	    set n 0
	    set r {}

	    $traverse foreach path {
		set rcs [fileutil::stripPath $base $path]
		if {[IsCVSAdmin    $rcs]}  continue
		if {![IsRCSArchive $path]} continue

		set usr [UserPath $rcs isattic]
		if {[IsSuperceded $base $rcs $usr $isattic]} continue

		log write 1 collar "Found   $rcs"
		$project add $rcs $usr

		incr n
		log progress 0 collar $n {}
	    }

	    $traverse destroy
	}
	return
    }

    typemethod ignore_conflicting_attics {} {
	set ignore 1
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable ignore 0

    proc IsRCSArchive {path} {
	if {![string match *,v $path]}     {return 0}
	if {[fileutil::test $path fr msg]} {return 1}
	trouble warn $msg
	return 0
    }

    proc IsCVSAdmin {rcs} {
	if {![string match CVSROOT/* $rcs]} {return 0}
	log write 2 collar "Ignored $rcs, administrative archive"
	return 1
    }

    proc UserPath {rcs iav} {
	upvar 1 $iav isattic

	# Derive the user-visible path from the rcs path. Meaning:
	# Chop off the ",v" suffix, and remove a possible "Attic".

	set f [string range $rcs 0 end-2]

	if {"Attic" eq [lindex [file split $rcs] end-1]} {

	    # The construction below ensures that Attic/X maps to X
	    # instead of ./X. Otherwise, Y/Attic/X maps to Y/X.

	    set fx [file dirname [file dirname $f]]
	    set f  [file tail $f]
	    if {$fx ne "."} { set f [file join $fx $f] }

	    set isattic 1
	} else {
	    set isattic 0
	}

	return $f
    }

    proc IsSuperceded {base rcs usr isattic} {
	if {!$isattic}                   {return 0}
	if {![file exists $base/$usr,v]} {return 0}

	# We have a regular archive and an Attic archive refering to
	# the same user visible file. Ignore the file in the Attic.
	#
	# By default this is a problem causing an abort after the pass
	# has completed. The user can however force us to ignore it.
	# In that case the warning is still printed, but will not
	# induce an abort any longer.

	if {$ignore} {
	    log write 2 collar "Ignored $rcs, superceded archive"
	} else {
	    trouble warn       "Ignored $rcs, superceded archive"
	}
	return 1
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export collar
    namespace eval collar {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collar
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collar 1.0
return
