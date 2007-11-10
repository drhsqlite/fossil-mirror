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

## Revisions per project, aka Changesets. These objects are first used
## in pass 5, which creates the initial set covering the repository.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::fossil::import::cvs::state        ; # State storage.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {project cstype srcid revisions} {
	set myid        [incr mycounter]
	set myproject   $project
	set mytype      $cstype	  
	set mysrcid	$srcid	  
	set myrevisions $revisions
	return
    }

    method persist {} {
	set tid $mycstype($mytype)
	set pid [$myproject id]
	set pos 0

	state transaction {
	    state run {
		INSERT INTO changeset (cid,   pid,  type, src)
		VALUES                ($myid, $pid, $tid, $mysrcid);
	    }

	    foreach rid $myrevisions {
		state run {
		    INSERT INTO csrevision (cid,   pos,  rid)
		    VALUES                 ($myid, $pos, $rid);
		}
		incr pos
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myid        ; # Id of the cset for the persistent state.
    variable myproject   ; # Reference of the project object the changeset belongs to.
    variable mytype      ; # rev or sym, where the cset originated from.
    variable mysrcid     ; # id of the metadata or symbol the cset is based on.
    variable myrevisions ; # List of the file level revisions in the cset.

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable mycounter        0 ; # Id counter for csets.
    typevariable mycstype -array {} ; # Map cstypes to persistent ids.

    typemethod getcstypes {} {
	foreach {tid name} [state run {
	    SELECT tid, name FROM cstype;
	}] { set mycstype($name) $tid }
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export rev
    namespace eval rev {
	namespace import ::vc::fossil::import::cvs::state
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::rev 1.0
return
