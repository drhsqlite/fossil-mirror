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

## Pass IV. Coming after the symbol collation pass this pass now
## removes all revisions and symbols referencing any of the excluded
## symbols from the persistent database.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    FilterSymbols \
    {Filter symbols, remove all excluded pieces} \
    ::vc::fossil::import::cvs::pass::filtersym

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::filtersym {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define names and structure of the persistent state of this
	# pass.

	state reading symbol
	state reading blocker
	state reading parent
	state reading preferedparent
	state reading revision
	state reading branch
	state reading tag
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	# The results of this pass are fully in the persistent state,
	# there is nothing to load for the next one.
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	# The removal of excluded symbols and everything referencing
	# to them is done completely in the database.

	state transaction {
	    FilterExcludedSymbols
	}

	log write 1 filtersym "Filtering completed"
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

    proc FilterExcludedSymbols {} {
	state run {
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export filtersym
    namespace eval filtersym {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::log
	log register filtersym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::filtersym 1.0
return
