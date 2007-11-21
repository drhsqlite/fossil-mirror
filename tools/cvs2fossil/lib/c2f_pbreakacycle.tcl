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

## Pass VIII. This is the final pass for breaking changeset dependency
## cycles. The two previous passes broke cycles covering revision and
## symbol changesets, respectively. This pass now breaks any remaining
## cycles each of which has to contain at least one revision and at
## least one symbol changeset.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require struct::list                              ; # Higher order list operations.
package require vc::tools::log                            ; # User feedback.
package require vc::fossil::import::cvs::repository       ; # Repository management.
package require vc::fossil::import::cvs::cyclebreaker     ; # Breaking dependency cycles.
package require vc::fossil::import::cvs::state            ; # State storage.
package require vc::fossil::import::cvs::project::rev     ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    BreakAllCsetCycles \
    {Break Remaining ChangeSet Dependency Cycles} \
    ::vc::fossil::import::cvs::pass::breakacycle

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::breakacycle {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.
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

	cyclebreaker precmd   [myproc BreakRetrogradeBranches]
	cyclebreaker savecmd  [myproc SaveOrder]
	cyclebreaker breakcmd [myproc BreakCycle]

	state transaction {
	    cyclebreaker run break-all [myproc Changesets]
	}

	repository printcsetstatistics
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

    proc Changesets {} { project::rev all }

    # # ## ### ##### ######## #############

    proc BreakRetrogradeBranches {graph} {
    }

    # # ## ### ##### ######## #############

    proc SaveOrder {cset pos} {
    }

    # # ## ### ##### ######## #############

    proc BreakCycle {graph} {
	cyclebreaker break $graph
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export breakacycle
    namespace eval breakacycle {
	namespace import ::vc::fossil::import::cvs::cyclebreaker
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register breakacycle
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::breakacycle 1.0
return
