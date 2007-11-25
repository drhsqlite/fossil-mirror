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

## Pass X. This pass goes over all changesets and sorts them
## topologically. It assumes that there are no cycles which could
## impede it, any remaining having been broken by the previous two
## passes, and aborts if that condition doesn't hold.

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
    AllTopologicalSort \
    {Topologically Sort All ChangeSets} \
    ::vc::fossil::import::cvs::pass::atopsort

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::atopsort {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state reading csorder

	state writing cstimestamp {
	    -- Commit order of all changesets based on their
	    -- dependencies, plus a monotonically increasing
	    -- timestamp.

	    cid  INTEGER  NOT NULL  REFERENCES changeset,
	    pos  INTEGER  NOT NULL,
	    date INTEGER  NOT NULL,
	    UNIQUE (cid),
	    UNIQUE (pos),
	    UNIQUE (date)
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

	cyclebreaker savecmd  [myproc SaveTimestamps]

	state transaction {
	    LoadSymbolChangesets
	    cyclebreaker run tsort-all [myproc Changesets]
	}
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard cstimestamp
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Changesets {} { project::rev all }

    proc LoadSymbolChangesets {} {
	set mysymchangesets [struct::list filter [project::rev all] [myproc IsBySymbol]]
	return
    }

    proc IsBySymbol {cset} { $cset bysymbol }

    proc SaveTimestamps {graph at cset} {
	set cid [$cset id]

	set date [GetTime [lindex [$graph node get $cset timerange] 1] \
		      [struct::set contain $mysymchangesets $cset]]

	log write 4 atopsort "Changeset @ $at: [$cset str]"
	state run {
	    INSERT INTO cstimestamp (cid,  pos, date)
	    VALUES                  ($cid, $at, $date)
	}
	return
    }

    proc GetTime {stamp expectchange} {
	::variable mylasttimestamp
	if {$stamp > $mymaxtimestamp} {
	    # A timestamp in the future is believed to be bogus and
	    # shifted backwars in time to prevent it from forcing
	    # other timestamps to be pushed even further in the
	    # future.

	    # From cvs2svn: Note that this is not nearly a complete
	    # solution to the bogus timestamp problem.  A timestamp in
	    # the future still affects the ordering of changesets, and
	    # a changeset having such a timestamp will not be
	    # committed until all changesets with earlier timestamps
	    # have been committed, even if other changesets with even
	    # earlier timestamps depend on this one.

	    incr mylasttimestamp
	    if {!$expectchange} {
		log write 4 atopsort "Timestamp [clock format $stamp] is in the future; shifted back to [clock format $mylasttimestamp]"
	    }
	} elseif {$stamp < ($mylasttimestamp)+1} {
	    incr mylasttimestamp
	    if {!$expectchange} {
		log write 4 atopsort "Timestamp [clock format $stamp] adjusted to [clock format $mylasttimestamp]"
	    }
	} else {
	    set mylasttimestamp $stamp
	}
	return $mylasttimestamp
    }

    typevariable mysymchangesets {} ; # Set of the symbol changesets.
    typevariable mylasttimestamp 0  ; # Last delivered timestamp.
    typevariable mymaxtimestamp 

    typeconstructor {
	# The maximum timestamp considered as reasonable is
	# "now + 1 day".
	set  mymaxtimestamp [clock seconds]
	incr mymaxtimestamp 86400 ; # 24h * 60min * 60sec
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
    namespace export atopsort
    namespace eval atopsort {
	namespace import ::vc::fossil::import::cvs::cyclebreaker
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register atopsort
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::atopsort 1.0
return
