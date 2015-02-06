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

## Pass VI. This pass computes the dependencies between the changesets
## from the file level dependencies and stores them in the state for
## use by the cycle breaker and topological sorting passes.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::misc                       ; # Text formatting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::rev ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    CsetDeps \
    {Compute and cache ChangeSet Dependencies} \
    ::vc::fossil::import::cvs::pass::csetdeps

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::csetdeps {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state use project
	state use file
	state use revision
	state use revisionbranchchildren
	state use branch
	state use tag
	state use symbol
	state use preferedparent
	state use changeset
	state use csitem

	# A table listing for each changeset the set of successor
	# changesets. The predecessor information is implied.

	state extend cssuccessor {
	    cid  INTEGER  NOT NULL  REFERENCES changeset,
	    nid  INTEGER  NOT NULL  REFERENCES changeset,
	    UNIQUE (cid,nid)
	} {cid nid}
	# Index on both columns for fast forward and back retrieval.
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	state use cssuccessor
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	set n   0
	set max [llength [project::rev all]]

	foreach cset [project::rev all] {
	    log progress 2 csetdeps $n $max
	    # NOTE: Consider to commit only every N calls.
	    state transaction {
		$cset determinesuccessors
	    }
	    incr n
	}
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard cssuccessor
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
    namespace export csetdeps
    namespace eval csetdeps {
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::log
	log register csetdeps
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::csetdeps 1.0
return
