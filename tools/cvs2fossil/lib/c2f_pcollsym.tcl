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

## Pass III. This pass divides the symbols collected by the previous
## pass into branches, tags, and excludes. The latter are __not__
## deleted by this pass, only marked. It is the next pass,
## 'FilterSym', which performs the actual deletion.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
#package require fileutil::traverse                    ; # Directory traversal.
#package require fileutil                              ; # File & path utilities.
#package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
#package require vc::fossil::import::cvs::pass         ; # Pass management.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    CollateSymbols \
    {Collate symbols} \
    ::vc::fossil::import::cvs::pass::collsym

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::collsym {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define names and structure of the persistent state of this
	# pass.

	state reading symbol
	state reading blocker
	state reading parent

	return
    }

    typemethod load {} {
	# TODO
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	state transaction {
	    repository   determinesymboltypes

	    project::sym printrulestatistics
	    project::sym printtypestatistics
	}

	log write 1 collsym "Collation completed"
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

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export collsym
    namespace eval collsym {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	#namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collsym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collsym 1.0
return
