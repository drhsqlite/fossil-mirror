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

    constructor {date author state thefile} {
	return
    }

    method hascommitmsg {} {
	# TODO: check that we have the commit message
	return 0
    }

    method setcommitmsg {cm} {
    }

    method settext {text} {
    }

    # # ## ### ##### ######## #############
    ## Type API

    typemethod istrunkrevnr {revnr} {
	return [expr {[llength [split $revnr .]] == 1}]
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
	    set branchnr ${head}.$tail
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

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    #pragma -hastypemethods no  ; # type is not relevant.
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
