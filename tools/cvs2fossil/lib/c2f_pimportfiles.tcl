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
## revisions of all files into one or more fossil repositories, one
## per project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require vc::tools::log                            ; # User feedback.
package require vc::fossil::import::cvs::repository       ; # Repository management.
package require vc::fossil::import::cvs::state            ; # State storage.
package require vc::fossil::import::cvs::fossil           ; # Access to fossil repositories.

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    ImportFiles \
    {Import the file revisions into fossil repositories} \
    ::vc::fossil::import::cvs::pass::importfiles

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::importfiles {
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

	# Discard on setup. Do not discard on deferal.
	state discard revuuid
	state extend  revuuid {
	    rid   INTEGER NOT NULL  REFERENCES revision UNIQUE,
	    uuid  INTEGER NOT NULL  -- fossil id of the revision
	    --                         unique within the project
	}

	# Remember the locations of the scratch data createdby this
	# pass, for use by the next (importing changesets).
	state discard space
	state extend  space {
	    pid        INTEGER  NOT NULL  REFERENCES project,
	    repository TEXT     NOT NULL,
	    workspace  TEXT     NOT NULL
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
	    log write 1 importfiles {Importing project "[$project base]"}

	    set pid    [$project id]
	    set fossil [fossil %AUTO%]
	    $fossil initialize

	    state transaction {
		# Layer I: Files and their revisions
		foreach file [$project files] {
		    $file pushto $fossil
		}

		# Save the scratch locations, needed by the next pass.
		struct::list assign [$fossil space] r w
		state run {
		    DELETE FROM space
		    WHERE pid = $pid
		    ;
		    INSERT INTO space (pid, repository, workspace)
		    VALUES            ($pid, $r, $w);
		}
	    }

	    $fossil destroy
	}
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	# Not discarding revuuid/space here, allow us to keep the info
	# for the next pass even if the revisions are recomputed.
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
    namespace export importfiles
    namespace eval importfiles {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::fossil
	namespace import ::vc::tools::log
	log register importfiles
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::importfiles 1.0
return
