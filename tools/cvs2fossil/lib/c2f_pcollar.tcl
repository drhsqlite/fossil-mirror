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
package require vc::fossil::import::cvs::state      ; # State storage

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
	# Define names and structure of the persistent state of this
	# pass.

	# We deal with repository projects, and the rcs archive files
	# in the projects.

	# For the first, projects, we keep their names, which are
	# their paths relative to the base directory of the whole
	# repository. These have to be globally unique, i.e. no two
	# projects can have the same name.

	# For the files we keep their names, which are their paths
	# relative to the base directory of the whole project! These
	# have to be unique within a project, however globally this
	# does not hold, a name may occur several times, in different
	# projects. We further store the user visible file name
	# associated with the rcs archive.

	# Both projects and files are identified by globally unique
	# integer ids, automatically assigned by the database.

	state writing project {
	    pid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    name TEXT     NOT NULL  UNIQUE
	}
	state writing file {
	    fid     INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    pid     INTEGER  NOT NULL  REFERENCES project,       -- project the file belongs to
	    name    TEXT     NOT NULL,
	    visible TEXT     NOT NULL,
	    exec    INTEGER  NOT NULL, -- boolean, 'file executable'.
	    UNIQUE (pid, name)         -- file names are unique within a project
	}
	return
    }

    typemethod load {} {	
	# Pass manager interface. Executed for all passes before the
	# run passes, to load all data of their pass from the state,
	# as if it had been computed by the pass itself.

	state reading project
	state reading file

	repository load
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	set rbase [repository base?]
	foreach project [repository projects] {
	    set base [file join $rbase [$project base]]
	    log write 1 collar "Scan $base"

	    set traverse [fileutil::traverse %AUTO% $base \
			      -prefilter [myproc FilterAtticSubdir $base]]
	    set n 0
	    set r {}

	    $traverse foreach path {
		set rcs [fileutil::stripPath $base $path]
		if {[IsCVSAdmin    $rcs]}  continue
		if {![IsRCSArchive $path]} continue

		set usr [UserPath $rcs isattic]
		if {[IsSuperceded $base $rcs $usr $isattic]} continue

		if {
		    [file exists      $base/$usr] &&
		    [file isdirectory $base/$usr]
		} {
		    trouble fatal "Directory name conflicts with filename."
		    trouble fatal "Please remove or rename one of the following:"
		    trouble fatal "    $base/$usr"
		    trouble fatal "    $base/$rcs"
		    continue
		}

		log write 4 collar "Found   $rcs"
		$project addfile $rcs $usr [file executable $rcs]

		incr n
		if {[log verbosity?] < 4} {
		    log progress 0 collar $n {}
		}
	    }

	    $traverse destroy
	}

	repository printstatistics
	repository persist

	log write 1 collar "Scan completed"
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard project
	state discard file
	return
    }

    typemethod ignore_conflicting_attics {} {
	set myignore 1
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable myignore 0

    proc FilterAtticSubdir {base path} {
	# This command is used by the traverser to prevent it from
	# scanning into subdirectories of an Attic. We get away with
	# checking the immediate parent directory of the current path
	# as our rejection means that deeper path do not occur.

	if {[file tail [file dirname $path]] eq "Attic"} {
	    set ad [fileutil::stripPath $base $path]
	    log write 1 collar "Directory $ad found in Attic, ignoring."
	    return 0
	}
	return 1
    }

    proc IsRCSArchive {path} {
	if {![string match *,v $path]}     {return 0}
	if {[fileutil::test $path fr msg]} {return 1}
	trouble warn $msg
	return 0
    }

    proc IsCVSAdmin {rcs} {
	if {![string match CVSROOT/* $rcs]} {return 0}
	log write 4 collar "Ignored $rcs, administrative archive"
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
	::variable myignore

	if {!$isattic}                   {return 0}
	if {![file exists $base/$usr,v]} {return 0}

	# We have a regular archive and an Attic archive refering to
	# the same user visible file. Ignore the file in the Attic.
	#
	# By default this is a problem causing an abort after the pass
	# has completed. The user can however force us to ignore it.
	# In that case the warning is still printed, but will not
	# induce an abort any longer.

	if {$myignore} {
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
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collar
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collar 1.0
return
