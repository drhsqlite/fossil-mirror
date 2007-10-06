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

## Repository manager. Keeps projects and their files around.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                          ; # Required runtime.
package require snit                             ; # OO system.
package require vc::tools::trouble               ; # Error reporting.
package require vc::tools::log                   ; # User feedback.
package require vc::tools::misc                  ; # Text formatting
package require vc::fossil::import::cvs::project ; # CVS projects
package require vc::fossil::import::cvs::state   ; # State storage
package require struct::list                     ; # List operations.
package require fileutil                         ; # File operations.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::repository {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod base {path} {
	# Could be checked, easier to defer to the overall validation.
	set mybase $path
	return
    }

    typemethod add {path} {
	# Most things cannot be checked immediately, as the base is
	# not known while projects are added. We can and do check for
	# uniqueness. We accept multiple occurences of a name, and
	# treat them as a single project.

	if {[lsearch -exact $myprojpaths $path] >= 0} return
	lappend myprojpaths $path
	return
    }

    typemethod projects {} {
	# TODO: Loading from the state database if CollAr is skipped
	# in a run.

	return [TheProjects]
    }

    typemethod base? {} { return $mybase }

    typemethod validate {} {
	if {![IsRepositoryBase $mybase msg]} {
	    trouble fatal $msg
	    # Without a good base directory checking any projects is
	    # wasted time, so we leave now.
	    return
	}
	foreach pp $myprojpaths {
	    if {![IsProjectBase $mybase/$pp $mybase/CVSROOT msg]} {
		trouble fatal $msg
	    }
	}
	return
    }

    typemethod printstatistics {} {
	set prlist [TheProjects]
	set npr [llength $prlist]

	log write 2 repository "Scanned [nsp $npr project]"

	if {$npr > 1} {
	    set  bmax [max [struct::list map $prlist [myproc .BaseLength]]]
	    incr bmax 2
	    set  bfmt %-${bmax}s

	    set  nmax [max [struct::list map $prlist [myproc .NFileLength]]]
	    set  nfmt %${nmax}s
	} else {
	    set bfmt %s
	    set nfmt %s
	}

	set keep {}
	foreach p $prlist {
	    set nfiles [llength [$p files]]
	    set line "Project [format $bfmt \"[$p printbase]\"] : [format $nfmt $nfiles] [sp $nfiles file]"
	    if {$nfiles < 1} {
		append line ", dropped"
	    } else {
		lappend keep $p
	    }
	    log write 2 repository $line
	}

	if {![llength $keep]} {
	    trouble warn "Dropped all projects"
	} elseif {$npr == [llength $keep]} {
	    log write 2 repository "Keeping all projects"
	} else {
	    log write 2 repository "Keeping [nsp [llength $keep] project]"
	    trouble warn "Dropped [nsp [expr {$npr - [llength $keep]}] {empty project}]"
	}

	# Keep reduced set of projects.
	set projects $keep
	return
    }

    typemethod persist {} {
	state transaction {
	    foreach p [TheProjects] { $p persist }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    typevariable mybase      {}
    typevariable myprojpaths {}
    typevariable myprojects  {}

    # # ## ### ##### ######## #############
    ## Internal methods

    proc .BaseLength {p} {
	return [string length [$p printbase]]
    }

    proc .NFileLength {p} {
	return [string length [llength [$p files]]]
    }

    proc IsRepositoryBase {path mv} {
	upvar 1 $mv msg mybase mybase
	if {![fileutil::test $mybase         edr msg {CVS Repository}]}      {return 0}
	if {![fileutil::test $mybase/CVSROOT edr msg {CVS Admin Directory}]} {return 0}
	return 1
    }

    proc IsProjectBase {path admin mv} {
	upvar 1 $mv msg
	if {![fileutil::test $path edr msg Project]} {return 0}
	if {
	    ($path eq $admin) ||
	    [string match $admin/* $path]
	} {
	    set msg "Administrative subdirectory $path cannot be a project"
	    return 0
	}
	return 1
    }

    proc TheProjects {} {
	upvar 1 myprojects myprojects myprojpaths myprojpaths mybase mybase

	if {![llength $myprojects]} {
	    set myprojects [EmptyProjects $myprojpaths]
	}
	return $myprojects
    }

    proc EmptyProjects {projpaths} {
	upvar 1 mybase mybase
	set res {}
	if {[llength $projpaths]} {
	    foreach pp $projpaths {
		lappend res [project %AUTO% $pp]
	    }
	} else {
	    # Base is the single project.
	    lappend res [project %AUTO% ""]
	}
	return $res
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export repository
    namespace eval repository {
	namespace import ::vc::fossil::import::cvs::project
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register repository
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::repository 1.0
return
