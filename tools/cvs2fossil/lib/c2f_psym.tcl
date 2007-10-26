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

## Symbols (Tags, Branches) per project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                 ; # Required runtime.
package require snit                                    ; # OO system.
package require vc::fossil::import::cvs::state          ; # State storage.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {name id project} {
	set myname    $name
	set myid      $id
	set myproject $project
	return
    }

    method name {} { return $myname }
    method id   {} { return $myid   }

    # # ## ### ##### ######## #############

    method persistrev {} {
	set pid [$myproject id]

	# TODO: Compute the various counts. All the necessary
	# TODO: information is already in the database. Actually it
	# TODO: never was in memory.

	state transaction {
	    state run {
		INSERT INTO symbol ( sid,   pid,  name,   type,     tag_count, branch_count, commit_count)
		VALUES             ($myid, $pid, $myname, $myundef, 0,         0,            0);
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myproject {} ; # Reference to the project object
			    # containing the symbol.
    variable myname    {} ; # The symbol's name
    variable myid      {} ; # Repository wide numeric id of the
			    # symbol. This implicitly encodes the
			    # project as well.

    typevariable mytag    1 ; # Code for symbols which are tags.
    typevariable mybranch 2 ; # Code for symbols which are branches.
    typevariable myundef  3 ; # Code for symbols of unknown type.

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export sym
    namespace eval sym {
	namespace import ::vc::fossil::import::cvs::state
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::sym 1.0
return
