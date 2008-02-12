## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007-2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Pass XII. This is the first of the backend passes. It imports the
## changesets constructed by the previous passes, and all file
## revisions they refer to into one or more fossil repositories, one
## per project.

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
    Import \
    {Import the changesets and file revisions into fossil repositories} \
    ::vc::fossil::import::cvs::pass::import

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::import {
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

	# This data is actually transient, confined to this pass. We
	# use the state storage only to keep the RAM usage low.
	state extend revuuid {
	    rid   INTEGER NOT NULL  REFERENCES revision UNIQUE,
	    uuid  INTEGER NOT NULL  -- fossil id of the revision
	    --                         unique within the project
	}

	# This information is truly non-transient, needed by the next
	# pass adding the tags.

	state extend csuuid {
	    cid   INTEGER NOT NULL  REFERENCES changeset UNIQUE,
	    uuid  INTEGER NOT NULL  -- fossil id of the changeset
	    --                         unique within the project
	}
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
	    log write 1 import {Importing project "[$project base]"}

	    set fossil [fossil %AUTO%]
	    set rstate [ristate %AUTO%]

	    state transaction {
		# Layer I: Files and their revisions
		foreach file [$project files] {
		    $file pushto $fossil
		}
		# Layer II: Changesets
		foreach {revision date} [$project revisionsinorder] {
		    $revision pushto $fossil $date $rstate
		}
	    }

	    $rstate destroy

	    # At last copy the temporary repository file to its final
	    # destination and release the associated memory.

	    set destination [$project base]
	    if {$destination eq ""} {
		set destination [file tail [repository base?]]
	    }
	    append destination .fsl

	    $fossil finalize $destination
	}

	# This does not live beyond the pass. We use the state for the
	# data despite its transient nature to keep the memory
	# requirements down.
	#state discard revuuid
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
    namespace export import
    namespace eval import {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::fossil
	namespace import ::vc::fossil::import::cvs::ristate
	namespace import ::vc::tools::log
	log register import
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::import 1.0
return
