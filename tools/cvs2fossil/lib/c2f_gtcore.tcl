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

## This file provides a helper package implementing the core of
## traversing the nodes of a graph in topological order. This is used
## by the cycle breaker code (not yet), and the import backend.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                                   ; # Required runtime.
package require snit                                      ; # OO system.
package require struct::graph                             ; # Graph handling.
package require struct::list                              ; # Higher order list operations.
package require vc::tools::log                            ; # User feedback.
package require vc::tools::trouble                        ; # Error reporting.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::gtcore {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod savecmd   {cmd} { ::variable mysavecmd   $cmd ; return }
    typemethod cyclecmd  {cmd} { ::variable mycyclecmd  $cmd ; return }
    typemethod sortcmd   {cmd} { ::variable mysortcmd   $cmd ; return }
    typemethod datacmd   {cmd} { ::variable mydatacmd   $cmd ; return }
    typemethod formatcmd {cmd} { ::variable myformatcmd $cmd ; return }

    # # ## ### ##### ######## #############

    typemethod traverse {graph {label Traverse}} {
	InitializeCandidates $graph

	log write 3 gtcore {$label}

	set k   0
	set max [llength [$graph nodes]]

	while {1} {
	    while {[WithoutPredecessor $graph node]} {
		log progress 2 gtcore $k $max
		incr k

		ProcessedHook    $graph $node
		ShowPendingNodes $graph
		$graph node delete      $node
	    }

	    if {![llength [$graph nodes]]} break

	    CycleHook            $graph
	    InitializeCandidates $graph
	}

	log write 3 gtcore Done.
	ClearHooks
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    # Instead of searching the whole graph for the degree-0 nodes in
    # each iteration we compute the list once to start, and then only
    # update it incrementally based on the outgoing neighbours of the
    # node chosen for commit.

    proc InitializeCandidates {graph} {
	# bottom = list (list (node, range min, range max))
	::variable mybottom
	foreach node [$graph nodes] {
	    if {[$graph node degree -in $node]} continue
	    lappend mybottom [list $node [DataHook $graph $node]]
	}
	ScheduleCandidates $graph
	ShowPendingNodes   $graph
	return
    }

    proc WithoutPredecessor {graph nodevar} {
	::variable mybottom

	upvar 1 $nodevar node
	if {![llength $mybottom]} { return 0 }

	set node [lindex [lindex $mybottom 0] 0]
	set mybottom     [lrange $mybottom 1 end]
	set changed 0

	# Update list of nodes without predecessor, based on the
	# outgoing neighbours of the chosen node. This should be
	# faster than iterating of the whole set of nodes, finding all
	# without predecessors, sorting them by time, etc. pp.

	foreach out [$graph nodes -out $node] {
	    if {[$graph node degree -in $out] > 1} continue
	    # Degree-1 neighbour, will have no predecessors after the
	    # removal of n. Put on the list of candidates we can
	    # process.
	    lappend mybottom [list $out [DataHook $graph $out]]
	    set changed 1
	}
	if {$changed} {
	    ScheduleCandidates $graph
	}

	# We do not delete the node immediately, to allow the Save
	# procedure to save the dependencies as well (encoded in the
	# arcs).
	return 1
    }

    proc ScheduleCandidates {graph} {
	::variable mybottom
	::variable mysortcmd
	if {[llength $mysortcmd]} {
	    set mybottom [uplevel \#0 [linsert $mysortcmd end $graph $mybottom]]
	} else {
	    set mybottom [lsort -index 0 -dict $mybottom]
	}
	return
    }

    proc ShowPendingNodes {graph} {
	if {[log verbosity?] < 10} return
	::variable mybottom
	::variable myformatcmd

	log write 10 gtcore "Pending..............................."
	foreach item [struct::list map $mybottom \
			  [linsert $myformatcmd end $graph]] {
	    log write 10 gtcore "Pending:     $item"
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Callback invokation ...

    proc DataHook {graph node} {
	# Allow the user of the traverser to a client data to a node
	# in the list of nodes available for immediate processing.
	# This data can be used by the sort callback.

	::variable mydatacmd
	if {![llength $mydatacmd]} { return {} }

	return [uplevel \#0 [linsert $mydatacmd end $graph $node]]
    }

    proc FormatHook {graph item} {
	# Allow the user to format a pending item (node + client data)
	# according to its wishes.

	::variable myformatcmd
	if {![llength $myformatcmd]} { return $item }

	return [uplevel \#0 [linsert $myformatcmd end $graph $item]]
    }

    proc ProcessedHook {graph node} {
	# Give the user of the traverser the opportunity to work with
	# the node before it is removed from the graph.

	::variable mysavecmd
	if {![llength $mysavecmd]} return

	uplevel \#0 [linsert $mysavecmd end $graph $node]
	return
    }

    proc CycleHook {graph} {
	# Call out to the chosen algorithm for handling cycles. It is
	# an error to find a cycle if no hook was defined.

	::variable mycyclecmd
	if {![llength $mycyclecmd]} {
	    trouble fatal "Found a cycle, expecting none."
	    exit 1
	}

	uplevel \#0 [linsert $mycyclecmd end $graph]
	return
    }

    proc ClearHooks {} {
	::variable mysortcmd   {}
	::variable myformatcmd {}
	::variable mydatacmd   {}
	::variable mysavecmd   {}
	::variable mycyclecmd  {}
	return
    }

    # # ## ### ##### ######## #############

    typevariable mybottom    {} ; # List of the nodes pending traversal.

    typevariable mysortcmd   {} ; # Callback, sort list of pending nodes
    typevariable mydatacmd   {} ; # Callback, get client data for a pending node
    typevariable myformatcmd {} ; # Callback, format a pending node for display
    typevariable mysavecmd   {} ; # Callback, for each processed node.
    typevariable mycyclecmd  {} ; # Callback, when a cycle was encountered.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export gtcore
    namespace eval   gtcore {
	namespace import ::vc::tools::log
	namespace import ::vc::tools::trouble
	log register gtcore
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::gtcore 1.0
return
