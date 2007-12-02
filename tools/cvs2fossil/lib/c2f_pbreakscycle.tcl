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

## Pass IX. This pass goes over the set of symbol based changesets and
## breaks all dependency cycles they may be in. We need a dependency
## tree. Identical to pass VII, except for the selection of the
## changesets.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require struct::list                              ; # Higher order list operations.
package require vc::tools::log                            ; # User feedback.
package require vc::fossil::import::cvs::cyclebreaker     ; # Breaking dependency cycles.
package require vc::fossil::import::cvs::repository       ; # Repository management.
package require vc::fossil::import::cvs::state            ; # State storage.
package require vc::fossil::import::cvs::integrity        ; # State integrity checks.
package require vc::fossil::import::cvs::project::rev     ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    BreakSymCsetCycles \
    {Break Symbol ChangeSet Dependency Cycles} \
    ::vc::fossil::import::cvs::pass::breakscycle

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::breakscycle {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state use revision
	state use tag
	state use branch
	state use symbol
	state use changeset
	state use csitem
	state use cssuccessor
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	cyclebreaker breakcmd {::vc::fossil::import::cvs::cyclebreaker break}

	state transaction {
	    cyclebreaker run break-sym [myproc Changesets]
	}

	repository printcsetstatistics
	integrity changesets
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Changesets {} {
	log write 2 breakscycle {Selecting the symbol changesets}
	return [project::rev sym]
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export breakscycle
    namespace eval breakscycle {
	namespace import ::vc::fossil::import::cvs::cyclebreaker
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register breakscycle
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::breakscycle 1.0
return
