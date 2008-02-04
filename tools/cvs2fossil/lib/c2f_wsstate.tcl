## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Track the state of a cvs workspace as changesets are committed to
## it. Nothing actually happens in the filesystem, this is completely
## virtual.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require struct::list                        ; # List assignment

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::wsstate {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {lod} {
	# Start with an empty state
	set myname $lod
	return
    }

    method name {} { return $myname }

    method add {revisioninfo} {
	# revisioninfo = list (rid path label ...) /triples

	# Overwrite all changed files (identified by path) with the
	# new revisions. This keeps all unchanged files.

	# BUG / TODO for FIX: Have to recognize dead files, to remove
	# them. We need the per-file revision optype for this.

	foreach {rid path label} $revisioninfo {
	    set mystate($path) [list $rid $label]
	}
	return
    }

    method get {} {
	set res {}
	foreach path [lsort -dict [array names mystate]] {
	    struct::list assign $mystate($path) rid label
	    lappend res $rid $path $label
	}
	return $res
    }

    method defid {id} {
	set myid $id
	return
    }

    method getid {} { return $myid }

    # # ## ### ##### ######## #############
    ## State

    variable myname {}         ; # Name of the LOD the workspace is
				 # for.
    variable myid   {}         ; # Record id of the fossil manifest
				 # associated with the current state.
    variable mystate -array {} ; # Map from paths to the recordid of
				 # the file revision behind it, and
				 # the associated label for logging.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export wsstate
    namespace eval wsstate {
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::wsstate 1.0
return
