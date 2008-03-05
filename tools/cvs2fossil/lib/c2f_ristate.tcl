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

## Track the state of revision import. Essentially maps lines of
## developments to their workspace state.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require struct::list                          ; # List assignment
package require vc::fossil::import::cvs::wsstate      ; # Workspace state
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.
package require vc::tools::log                        ; # User feedback.
package require vc::tools::trouble                    ; # Error reporting.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::ristate {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {} {
	# Start with an empty state
	return
    }

    method new {lod {parentlod {}}} {
	# Create a workspace for a line of development (LOD). If a
	# parent LOD is specified let the new workspace inherit the
	# current state of the parent.

	log write 8 ristate {Open workspace for LOD "$lod"}

	integrity assert {
	    ![info exists mystate($lod)]
	} {Trying to override existing state for lod "$lod"}

	set wss [wsstate ${selfns}::%AUTO% $lod]
	set mystate($lod) $wss

	if {$parentlod ne ""} {
	    log write 8 ristate {Inheriting from workspace for LOD "$parentlod"}

	    integrity assert {
		[info exists mystate($parentlod)]
	    } {Trying to inherit from undefined lod "$parentlod"}

	    set pwss $mystate($parentlod)

	    $wss defstate  [$pwss getstate]
	    $wss defid     [$pwss getid]
	    $wss defparent $pwss
	}

	return $wss
    }

    method get {lod} { return $mystate($lod) }
    method has {lod} { return [info exists mystate($lod)] }

    method names {} { return [array names mystate] }

    method dup {dst _from_ src} {
	log write 8 ristate {Duplicate workspace for LOD "$dst" from "$src"}
	set mystate($dst) $mystate($src)
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable mystate -array {} ; # Map from lines of development
				 # (identified by name) to their
				 # workspace.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export ristate
    namespace eval ristate {
	namespace import ::vc::fossil::import::cvs::wsstate
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register ristate
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::ristate 1.0
return
