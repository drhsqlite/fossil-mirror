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

## Pass VI. This pass goes over the set of revision based changesets
## and breaks all dependency cycles they may be in. We need a
## dependency tree.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require struct::graph                         ; # Graph handling.
package require struct::list                          ; # Higher order list operations.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::rev ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    BreakRevCsetCycles \
    {Break Revision ChangeSet Dependency Cycles} \
    ::vc::fossil::import::cvs::pass::breakrcycle

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::pass::breakrcycle {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state writing csorder {
	    -- Commit order of changesets based on their dependencies
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
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	state reading revision

	# We create a graph of the revision changesets, using the file
	# level dependencies to construct a first approximation of
	# them at the project level. Then look for cycles in that
	# graph and break them.

	# 1. Create nodes for all relevant changesets and a mapping
	#    from the revisions to their changesets/nodes.

	log write 3 brkrcycle {Creating changeset graph, filling with nodes}

	set dg [struct::graph dg]

	state transaction {
	    foreach cset [project::rev all] {
		if {[$cset bysymbol]} continue
		dg node insert $cset
		dg node set    $cset timerange [$cset timerange]
	    }
	}

	# 2. Find for all relevant changeset their revisions and their
	#    dependencies. Map the latter back to changesets and
	#    construct the corresponding arcs.

	log write 3 brkrcycle {Setting up node dependencies}

	state transaction {
	    foreach cset [project::rev all] {
		if {[$cset bysymbol]} continue
		foreach succ [$cset successors] {
		    dg arc insert $cset $succ
		}
	    }
	}

	# 3. Lastly we iterate the graph topologically. We mark off
	#    the nodes which have no predecessors, in order from
	#    oldest to youngest, saving and removing dependencies. If
	#    we find no nodes without predecessors we have a cycle,
	#    and work on breaking it.

	log write 3 brkrcycle {Computing changeset order, breaking cycles}

	InitializeCandidates $dg
	state transaction {
	    while {1} {
		while {[WithoutPredecessor $dg n]} {
		    SaveAndRemove $dg $n
		}
		if {![llength [dg nodes]]} break
		set cycle [FindCycle $dg]
		BreakCycle $dg $cycle
	    }
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

    # Instead of searching the whole graph for the degree-0 nodes in
    # each iteration we compute the list once to start, and then only
    # update it incrementally based on the outgoing neighbours of the
    # node chosen for commit.

    proc InitializeCandidates {dg} {
	# bottom = list (list (node, range min, range max))
	::variable bottom
	foreach n [$dg nodes] {
	    if {[$dg node degree -in $n]} continue
	    lappend bottom [linsert [$dg node get $n timerange] 0 $n]
	}
	set bottom [lsort -index 1 -integer [lsort -index 2 -integer $bottom]]
	return
    }

    proc WithoutPredecessor {dg nv} {
	::variable bottom

	upvar 1 $nv n
	if {![llength $bottom]} { return 0 }

	set n [lindex [lindex $bottom 0] 0]
	set bottom [lrange $bottom 1 end]
	set changed 0

	# Update list of nodes without predecessor, based on the
	# outgoing neighbours of the chosen node. This should be
	# faster than iterating of the whole set of nodes, finding all
	# without predecessors, sorting them by time, etc. pp.
	foreach out [$dg nodes -out $n] {
	    if {[$dg node degree -in $out] > 1} continue
	    # Degree-1 neighbour, will have no predecessors after the
	    # removal of n. Put on the list.
	    lappend bottom [linsert [$dg node get $out timerange] 0 $out]
	    set changed 1
	}
	if {$changed} {
	    set bottom [lsort -index 1 -integer [lsort -index 2 -integer $bottom]]
	}

	# We do not delete the node immediately, to allow the Save
	# procedure to save the dependencies as well (encoded in the
	# arcs).
	return 1
    }

    proc SaveAndRemove {dg n} {
	::variable at
	set cid [$n id]

	log write 4 breakrcycle "Comitting @ $at: <$cid>"
	state run {
	    INSERT INTO csorder (cid,  pos)
	    VALUES              ($cid, $at)
	}
	# TODO: Write the project level changeset dependencies as well.
	incr at
	$dg node delete $n
	return
    }

    proc FindCycle {dg} {
	# This procedure is run if and only the graph is not empty and
	# all nodes have predecessors. This means that each node is
	# either part of a cycle or (indirectly) depending on a node
	# in a cycle. We can start at an arbitrary node, follow its
	# incoming edges to its predecessors until we see a node a
	# second time. That node closes the cycle and the beginning is
	# its first occurence. Note that we can choose an arbitrary
	# predecessor of each node as well, we do not have to search.

	# We record for each node the index of the first appearance in
	# the path, making it easy at the end to cut the cycle from
	# it.

	# Choose arbitrary node to start our search at.
	set start [lindex [$dg nodes] 0]

	# Initialize state, path of seen nodes, and when seen.
	set       path {}
	array set seen {}

	while {1} {
	    # Stop searching when we have seen the current node
	    # already, the circle has been closed.
	    if {[info exists seen($start)]} break
	    lappend path $start
	    set seen($start) [llength $path]
	    # Choose arbitrary predecessor
	    set start [lindex [$dg nodes -in $start] 0]
	}

	return [struct::list reverse [lrange $path $seen($start) end]]
    }

    proc BreakCycle {dg cycle} {
	trouble internal "Break cycle <$cycle>"
	return
    }

    typevariable at      0 ; # Counter for commit ids for the changesets.
    typevariable bottom {} ; # List of candidate nodes for committing.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export breakrcycle
    namespace eval breakrcycle {
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register brkrcycle
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::breakrcycle 1.0
return
