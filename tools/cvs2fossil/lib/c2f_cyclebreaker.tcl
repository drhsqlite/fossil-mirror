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

## This file provides a helper package for the passes 6 and 7 which
## contains the common code of the cycle breaking algorithm.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require struct::graph                             ; # Graph handling.
package require struct::list                              ; # Higher order list operations.
package require vc::tools::log                            ; # User feedback.
package require vc::tools::misc                           ; # Text formatting.
package require vc::fossil::import::cvs::project::rev     ; # Project level changesets
package require vc::fossil::import::cvs::project::revlink ; # Cycle links.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::cyclebreaker {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod run {changesets {savecmd {}}} {
	::variable save $savecmd
	::variable at   0

	# We create a graph of the revision changesets, using the file
	# level dependencies to construct a first approximation of the
	# dependencies at the project level. Then we look for cycles
	# in that graph and break them.

	# 1. Create nodes for all relevant changesets and a mapping
	#    from the revisions to their changesets/nodes.

	log write 3 cyclebreaker "Creating changeset graph, filling with nodes"
	log write 3 cyclebreaker "Adding [nsp [llength $changesets] node]"

	set dg [struct::graph dg]

	foreach cset $changesets {
	    dg node insert $cset
	    dg node set    $cset timerange [$cset timerange]
	}

	# 2. Find for all relevant changeset their revisions and their
	#    dependencies. Map the latter back to changesets and
	#    construct the corresponding arcs.

	log write 3 cyclebreaker {Setting up node dependencies}

	foreach cset $changesets {
	    foreach succ [$cset successors] {
		# Changesets may have dependencies outside of the
		# chosen set. These are ignored
		if {![dg node exists $succ]} continue
		dg arc insert $cset $succ
	    }
	}

	# 3. Lastly we iterate the graph topologically. We mark off
	#    the nodes which have no predecessors, in order from
	#    oldest to youngest, saving and removing dependencies. If
	#    we find no nodes without predecessors we have a cycle,
	#    and work on breaking it.

	log write 3 cyclebreaker {Now sorting the changesets, breaking cycles}

	InitializeCandidates $dg
	while {1} {
	    while {[WithoutPredecessor $dg n]} {
		SaveAndRemove $dg $n
	    }
	    if {![llength [dg nodes]]} break
	    BreakCycle $dg [FindCycle $dg]
	    InitializeCandidates $dg
	}

	log write 3 cyclebreaker Done.
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
	::variable save

	# Give the user of the cycle breaker the opportunity to work
	# with the changeset before it is removed from the graph.

	if {[llength $save]} {
	    uplevel #0 [linsert $save end $at $n]
	}

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
	    set seen($start) [expr {[llength $path]-1}]
	    # Choose arbitrary predecessor
	    set start [lindex [$dg nodes -in $start] 0]
	}

	return [struct::list reverse [lrange $path $seen($start) end]]
    }

    proc ID {cset} { return "<[$cset id]>" }

    proc BreakCycle {dg cycle} {
	# The cycle we have gotten is broken by breaking apart one or
	# more of the changesets in the cycle. This causes us to
	# create one or more changesets which are to be committed,
	# added to the graph, etc. pp.

	set cprint [join [struct::list map $cycle [myproc ID]] { }]

	lappend cycle [lindex $cycle 0] [lindex $cycle 1]
	set bestlink {}
	set bestnode {}

	foreach \
	    prev [lrange $cycle 0 end-2] \
	    cset [lrange $cycle 1 end-1] \
	    next [lrange $cycle 2 end] {

		# Each triple PREV -> CSET -> NEXT of changesets, a
		# 'link' in the cycle, is analysed and the best
		# location where to at least weaken the cycle is
		# chosen for further processing.

		set link [project::revlink %AUTO% $prev $cset $next]
		if {$bestlink eq ""} {
		    set bestlink $link
		    set bestnode $cset
		} elseif {[$link betterthan $bestlink]} {
		    $bestlink destroy
		    set bestlink $link
		    set bestnode $cset
		} else {
		    $link destroy
		}
	    }

	log write 5 breakrcycle "Breaking cycle ($cprint) by splitting changeset <[$bestnode id]>"

	set newcsets [$bestlink break]
	$bestlink destroy

        # At this point the old changeset (BESTNODE) is gone
        # already. We remove it from the graph as well and then enter
        # the fragments generated for it.

        $dg node delete $bestnode

	foreach cset $newcsets {
	    $dg node insert $cset
	    $dg node set    $cset timerange [$cset timerange]
	}

	foreach cset $newcsets {
	    foreach succ [$cset successors] {
		# The new changesets may have dependencies outside of
		# the chosen set. These are ignored
		if {![$dg node exists $succ]} continue
		$dg arc insert $cset $succ
	    }
	}
	return
    }

    typevariable at      0 ; # Counter for commit ids for the changesets.
    typevariable bottom {} ; # List of candidate nodes for committing.
    typevariable save   {} ; # The command to call for each processed node

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export cyclebreaker
    namespace eval cyclebreaker {
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	    namespace import ::vc::fossil::import::cvs::project::revlink
	}
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::log
	log register cyclebreaker
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::cyclebreaker 1.0
return
