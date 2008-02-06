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

## Pass VIII. This pass goes over the set of revision based changesets
## and sorts them topologically. It assumes that there are no cycles
## which could impede it, any having been broken by the previous pass,
## and aborts if that condition doesn't hold.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require struct::list                              ; # Higher order list operations.
package require vc::tools::log                            ; # User feedback.
package require vc::fossil::import::cvs::cyclebreaker     ; # Breaking dependency cycles.
package require vc::fossil::import::cvs::state            ; # State storage.
package require vc::fossil::import::cvs::project::rev     ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    RevTopologicalSort \
    {Topologically Sort Revision ChangeSets} \
    ::vc::fossil::import::cvs::pass::rtopsort

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::rtopsort {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state use revision
	state use symbol
	state use changeset
	state use csitem
	state use cstype
	state use cssuccessor

	state extend csorder {
	    -- Commit order of the revision changesets based on their
	    -- dependencies

	    cid INTEGER  NOT NULL  REFERENCES changeset,
	    pos INTEGER  NOT NULL,
	    UNIQUE (cid),
	    UNIQUE (pos)
	}
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	state use changeset
	project::rev loadcounter
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	set len [string length [project::rev num]]
	set myatfmt %${len}s
	incr len 12
	set mycsfmt %${len}s

	cyclebreaker savecmd  [myproc SaveOrder]

	state transaction {
	    cyclebreaker run tsort-rev [myproc Changesets]
	}
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard csorder
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Changesets {} {
	log write 2 breakscycle {Selecting the revision changesets}
	return [project::rev rev]
    }

    proc SaveOrder {graph at cset} {
	::variable myatfmt
	::variable mycsfmt

	set cid [$cset id]

	log write 4 rtopsort "Changeset @ [format $myatfmt $at]: [format $mycsfmt [$cset str]] '[$cset lod]' <<[FormatTR $graph $cset]>>"
	state run {
	    INSERT INTO csorder (cid,  pos)
	    VALUES              ($cid, $at)
	}
	return
    }

    proc FormatTR {graph cset} {
	return [join [struct::list map [$graph node set $cset timerange] {clock format}] { -- }]
    }

    typevariable myatfmt ; # Format for log output to gain better alignment of the various columns.
    typevariable mycsfmt ; # Ditto for the changesets.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export rtopsort
    namespace eval rtopsort {
	namespace import ::vc::fossil::import::cvs::cyclebreaker
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register rtopsort
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::rtopsort 1.0
return
