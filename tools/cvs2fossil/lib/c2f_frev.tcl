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

## Revisions per file.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {revnr date author state thefile} {
	set myrevnr  $revnr
	set mydate   $date
	set myauthor $author
	set mystate  $state
	set myfile   $thefile
	return
    }

    method hascommitmsg {} { return $myhascm }

    method setcommitmsg {cm} {
	set mycommitmsg $cm
	set myhascm 1
	return
    }

    method settext {text} {
	set mytext $text
	return
    }

    method setbranch {branchnr} {
	set mybranchnr $branchnr
	return
    }

    # # ## ### ##### ######## #############
    ## Type API

    typemethod istrunkrevnr {revnr} {
	return [expr {[llength [split $revnr .]] == 2}]
    }

    typemethod 2branchnr {revnr} {
	# Input is a branch revision number, i.e. a revision number
	# with an even number of components; for example '2.9.2.1'
	# (never '2.9.2' nor '2.9.0.2').  The return value is the
	# branch number (for example, '2.9.2').  For trunk revisions,
	# like '3.4', we return the empty string.

	if {[$type istrunkrevnr $revnr]} {
	    return ""
	}
	return [join [lrange [split $revnr .] 0 end-1] .]
    }

    typemethod isbranchrevnr {revnr _ bv} {
	if {[regexp $mybranchpattern $revnr -> head tail]} {
	    upvar 1 $bv branchnr
	    set branchnr ${head}$tail
	    return 1
	}
	return 0
    }

    # # ## ### ##### ######## #############
    ## State

    typevariable mybranchpattern {^((?:\d+\.\d+\.)+)(?:0\.)?(\d+)$}
    # First a nonzero even number of digit groups with trailing dot
    # CVS then sticks an extra 0 in here; RCS does not.
    # And the last digit group.

    variable myrevnr     {} ; # Revision number of the revision.
    variable mydate      {} ; # Timestamp of the revision, seconds since epoch
    variable mystate     {} ; # State of the revision.
    variable myfile      {} ; # Ref to the file object the revision belongs to.
    variable myhascm     0  ; # Bool flag, set when the commit msg was set.
    variable mytext      {} ; # Range of the (delta) text for this revision in the file.

    # The meta data block used later to group revisions into changesets.
    # The project name factors into this as well, but is not stored
    # here. The name is acessible via myfile's project.

    variable myauthor    {} ; # Name of the user who committed the revision.
    variable mycommitmsg {} ; # The message entered as part of the commit.
    variable mybranchnr  {} ; # The number of the branch the commit was done on.

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::file {
    namespace export rev
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file::rev 1.0
return
