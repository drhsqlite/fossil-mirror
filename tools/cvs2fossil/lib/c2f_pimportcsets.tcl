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

## Pass XIII. This is the second of the backend passes. It imports the
## changesets constructed by the previous passes into one or more
## fossil repositories, one per project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require vc::tools::log                            ; # User feedback.
package require vc::fossil::import::cvs::repository       ; # Repository management.
package require vc::fossil::import::cvs::state            ; # State storage.
package require vc::fossil::import::cvs::fossil           ; # Access to fossil repositories.
package require vc::fossil::import::cvs::ristate          ; # Import state (revisions)

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    ImportCSets \
    {Import the changesets into fossil repositories} \
    ::vc::fossil::import::cvs::pass::importcsets

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::importcsets {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state use project
	state use file
	state use revision
	state use meta
	state use author
	state use cmessage
	state use symbol
	state use space
	state use revuuid
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

	foreach project [repository projects] {
	    log write 1 importcsets {Importing project "[$project base]"}

	    set pid    [$project id]
	    set fossil [fossil %AUTO%]
	    struct::list assign [state run {
		SELECT repository, workspace
		FROM space
		WHERE pid = $pid
	    }] r w
	    $fossil load $r $w

	    set rstate [ristate %AUTO%]

	    state transaction {
		# Layer II: Changesets
		foreach {cset date} [$project changesetsinorder] {
		    $cset pushto $fossil $date $rstate
		}
	    }

	    $rstate destroy
	    $fossil destroy
	}
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
    namespace export importcsets
    namespace eval importcsets {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::fossil
	namespace import ::vc::fossil::import::cvs::ristate
	namespace import ::vc::tools::log
	log register importcsets
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::importcsets 1.0
return
