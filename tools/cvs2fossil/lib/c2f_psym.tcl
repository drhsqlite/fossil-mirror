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

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {name} {
	set myname $name
	return
    }

    method name {} { return $myname }

    # # ## ### ##### ######## #############
    ## State

    variable myname {}

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
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::sym 1.0
return
