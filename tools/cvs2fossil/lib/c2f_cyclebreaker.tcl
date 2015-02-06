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
package require vc::tools::dot                            ; # User feedback. DOT export.
package require vc::tools::log                            ; # User feedback.
package require vc::tools::trouble                        ; # Error reporting.
package require vc::tools::misc                           ; # Text formatting.
package require vc::fossil::import::cvs::project::rev     ; # Project level changesets
package require vc::fossil::import::cvs::project::revlink ; # Cycle links.
package require vc::fossil::import::cvs::integrity        ; # State integrity checks.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::cyclebreaker {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod precmd {cmd} {
	::variable myprecmd $cmd
	return
    }

    typemethod savecmd {cmd} {
	::variable mysavecmd $cmd
	return
    }

    typemethod breakcmd {cmd} {
	::variable mybreakcmd $cmd
	return
    }

    # # ## ### ##### ######## #############

    typemethod dotsto {path} {
	::variable mydotdestination $path
	return
    }

    typemethod watch {id} {
	::variable mywatchids
	lappend mywatchids $id
    }

    typemethod dot {label changesets} {
	::variable mydotprefix $label
	::variable mydotid     0

	set dg [Setup $changesets 0]
	Mark $dg
	$dg destroy
	return
    }

    typemethod mark {graph suffix {subgraph {}}} {
	Mark $graph $suffix $subgraph
	return
    }

    # # ## ### ##### ######## #############

    typemethod run {label changesetcmd} {
	::variable myat        0
	::variable mydotprefix $label
	::variable mydotid     0

	# We create a graph of the revision changesets, using the file
	# level dependencies to construct a first approximation of the
	# dependencies at the project level. Then we look for cycles
	# in that graph and break them.

	# 1. Create nodes for all relevant changesets and a mapping
	#    from the revisions to their changesets/nodes.

	set changesets [uplevel #0 $changesetcmd]
	set dg [Setup $changesets]

	# 3. Lastly we iterate the graph topologically. We mark off
	#    the nodes which have no predecessors, in order from
	#    oldest to youngest, saving and removing dependencies. If
	#    we find no nodes without predecessors we have a cycle,
	#    and work on breaking it.

	log write 3 cyclebreaker {Traverse changesets}

	InitializeCandidates $dg

	set k   0
	set max [llength [$dg nodes]]
	while {1} {
	    while {[WithoutPredecessor $dg n]} {
		log progress 2 cyclebreaker $k $max ; incr k
		MarkWatch $dg
		ProcessedHook $dg $n $myat
		$dg node delete $n
		incr myat
		ShowPendingNodes
	    }

	    if {![llength [$dg nodes]]} break

	    BreakCycleHook       $dg
	    InitializeCandidates $dg
	    MarkWatch            $dg
	}

	$dg destroy

	log write 3 cyclebreaker Done.
	ClearHooks

	# Reread the graph and dump its final form, if graph export
	# was activated.

	::variable mydotdestination
	if {$mydotdestination eq ""} return

	set   dg [Setup [uplevel #0 $changesetcmd] 0]
	Mark $dg -done
	$dg destroy
	return
    }

    # # ## ### ##### ######## #############

    typemethod break-segment {graph} {
	BreakSegment $graph $path "segment ([project::rev strlist $path])"
	return
    }

    typemethod break {graph} {
	set cycle [FindCycle $graph]
	set label "cycle ([project::rev strlist $cycle])"

	# NOTE: cvs2svn uses the sequence "end-1, cycle, 0" to create
	#       the path from the cycle. The only effect I can see is
	#       that this causes the link-triples to be generated in a
	#       sightly different order, i.e. one link rotated to the
	#       right. This should have no effect on the search for
	#       the best of all.

	lappend cycle [lindex $cycle 0] [lindex $cycle 1]
	BreakSegment $graph $cycle $label
	return
    }

    typemethod replace {graph n replacements} {
	Replace $graph $n $replacements
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Setup {changesets {log 1}} {
	if {$log} {
	    log write 3 cyclebreaker "Creating graph of changesets"
	}

	set dg [struct::graph dg]

	set n 0
	set max [llength $changesets]

	foreach cset $changesets {
	    log progress 2 cyclebreaker $n $max
	    set tr [$cset timerange]
	    $dg node insert $cset
	    $dg node set    $cset timerange $tr
	    $dg node set    $cset label     "[$cset str]\\n[join [struct::list map $tr {::clock format}] "\\n"]"
	    $dg node set    $cset __id__    [$cset id]
	    $dg node set    $cset shape     [expr {[$cset bysymbol]
						   ? "ellipse"
						   : "box"}]
	    incr n
	}

	if {$log} {
	    log write 3 cyclebreaker "Has [nsp [llength $changesets] changeset]"
	}

	# 2. Find for all relevant changeset their revisions and their
	#    dependencies. Map the latter back to changesets and
	#    construct the corresponding arcs.

	set n 0
	foreach cset $changesets {
	    log progress 2 cyclebreaker $n $max
	    foreach succ [$cset successors] {
		# Changesets may have dependencies outside of the
		# chosen set. These are ignored
		if {![$dg node exists $succ]} continue
		$dg arc insert $cset $succ
		integrity assert {
		    $succ ne $cset
		} {[$cset reportloop 0]Changeset loop was not detected during creation}
	    }
	    incr n
	}

	if {$log} {
	    log write 3 cyclebreaker "Has [nsp [llength [$dg arcs]] dependency dependencies]"
	}

	# Run the user hook to manipulate the graph before
	# consummation.

	if {$log} { Mark $dg -start }
	MarkWatch $dg
	PreHook   $dg
	MarkWatch $dg
	return  $dg
    }

    # Instead of searching the whole graph for the degree-0 nodes in
    # each iteration we compute the list once to start, and then only
    # update it incrementally based on the outgoing neighbours of the
    # node chosen for commit.

    proc InitializeCandidates {dg} {
	# bottom = list (list (node, range min, range max))
	::variable mybottom
	foreach n [$dg nodes] {
	    if {[$dg node degree -in $n]} continue
	    lappend mybottom [linsert [$dg node get $n timerange] 0 $n]
	}
	ScheduleCandidates
	ShowPendingNodes
	return
    }

    proc WithoutPredecessor {dg nv} {
	::variable mybottom

	upvar 1 $nv n
	if {![llength $mybottom]} { return 0 }

	set n [lindex [lindex $mybottom 0] 0]
	set mybottom [lrange $mybottom 1 end]
	set changed 0

	# Update list of nodes without predecessor, based on the
	# outgoing neighbours of the chosen node. This should be
	# faster than iterating of the whole set of nodes, finding all
	# without predecessors, sorting them by time, etc. pp.
	foreach out [$dg nodes -out $n] {
	    if {[$dg node degree -in $out] > 1} continue
	    # Degree-1 neighbour, will have no predecessors after the
	    # removal of n. Put on the list.
	    lappend mybottom [linsert [$dg node get $out timerange] 0 $out]
	    set changed 1
	}
	if {$changed} {
	    ScheduleCandidates
	}

	# We do not delete the node immediately, to allow the Save
	# procedure to save the dependencies as well (encoded in the
	# arcs).
	return 1
    }

    proc ScheduleCandidates {} {
	::variable mybottom
	# Sort by cset object name, lower border of timerange, at last
	# by the upper border.
	set mybottom [lsort -index 2 -integer [lsort -index 1 -integer [lsort -index 0 -dict $mybottom]]]
	return
    }

    proc ShowPendingNodes {} {
	if {[log verbosity?] < 10} return
	::variable mybottom
	log write 10 cyclebreaker "Pending..............................."
	foreach item [struct::list map $mybottom [myproc FormatPendingItem]] {
	    log write 10 cyclebreaker "Pending:     $item"
	}
	return
    }

    proc FormatPendingItem {item} {
	join [list [[lindex $item 0] str] [clock format [lindex $item 1]] [clock format [lindex $item 2]]]
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

    proc BreakSegment {dg path label} {
	# The path, usually a cycle, we have gotten is broken by
	# breaking apart one or more of the changesets in the
	# cycle. This causes us to create one or more changesets which
	# are to be committed, added to the graph, etc. pp.

	set bestlink {}
	set bestnode {}

	foreach \
	    prev [lrange $path 0 end-2] \
	    cset [lrange $path 1 end-1] \
	    next [lrange $path 2 end] {

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

	log write 5 cyclebreaker "Breaking $label by splitting changeset [$bestnode str]"
	set ID [$bestnode id]
	Mark $dg -${ID}-before

	set newcsets [$bestlink break]
	$bestlink destroy

        # At this point the old changeset (BESTNODE) is gone
        # already. We remove it from the graph as well and then enter
        # the fragments generated for it.

	Replace $dg $bestnode $newcsets

	Mark $dg -${ID}-after
	return
    }

    # TODO: This should be a graph method.
    proc HasArc {dg a b} {
	#8.5: return [expr {$b in [$dg nodes -out $a]}]
	if {[lsearch -exact [$dg nodes -out $a] $b] < 0} { return 0 }
	return 1
    }

    proc Mark {dg {suffix {}} {subgraph {}}} {
	::variable mydotdestination
	if {$mydotdestination eq ""} return
	::variable mydotprefix
	::variable mydotid
	set fname $mydotdestination/${mydotprefix}${mydotid}${suffix}.dot
	file mkdir [file dirname $fname]
	dot write $dg $mydotprefix$suffix $fname $subgraph
	incr mydotid

	log write 5 cyclebreaker ".dot export $fname"
	return
    }

    proc Replace {dg n replacements} {
	# NOTE. We have to get the list of incoming neighbours and
	# recompute their successors after the new nodes have been
	# inserted. Their outgoing arcs will now go to one or both of
	# the new nodes, and not redoing them may cause us to forget
	# circles, leaving them in, unbroken.

	set pre [$dg nodes -in $n]

        $dg node delete $n

	foreach cset $replacements {
	    set tr [$cset timerange]
	    $dg node insert $cset
	    $dg node set    $cset timerange $tr
	    $dg node set    $cset label     "[$cset str]\\n[join [struct::list map $tr {::clock format}] "\\n"]"
	    $dg node set    $cset __id__    [$cset id]
	    $dg node set    $cset shape     [expr {[$cset bysymbol]
						   ? "ellipse"
						   : "box"}]
	}

	foreach cset $replacements {
	    foreach succ [$cset successors] {
		# The new changesets may have dependencies outside of
		# the chosen set. These are ignored
		if {![$dg node exists $succ]} continue
		$dg arc insert $cset $succ
		integrity assert {
		    $succ ne $cset
		} {[$cset reportloop 0]Changeset loop was not detected during creation}
	    }
	}
	foreach cset $pre {
	    foreach succ [$cset successors] {
		# Note that the arc may already exist in the graph. If
		# so ignore it. The new changesets may have
		# dependencies outside of the chosen set. These are
		# ignored
		if {![$dg node exists $succ]} continue
		if {[HasArc $dg $cset $succ]} continue;# TODO should be graph method.
		$dg arc insert $cset $succ
	    }
	}

	return
    }

    # # ## ### ##### ######## #############
    ## Callback invokation ...

    proc PreHook {graph} {
	# Give the user of the cycle breaker the opportunity to work
	# with the graph between setup and consummation.

	::variable myprecmd
	if {![llength $myprecmd]} return

	uplevel #0 [linsert $myprecmd end $graph]
	Mark $graph -pre-done
	return
    }

    proc ProcessedHook {dg cset pos} {
	# Give the user of the cycle breaker the opportunity to work
	# with the changeset before it is removed from the graph.

	::variable mysavecmd
	if {![llength $mysavecmd]} return

	uplevel #0 [linsert $mysavecmd end $dg $pos $cset]
	return
    }

    proc BreakCycleHook {graph} {
	# Call out to the chosen algorithm for cycle breaking. Finding
	# a cycle if no breaker was chosen is an error.

	::variable mybreakcmd
	if {![llength $mybreakcmd]} {
	    trouble fatal "Found a cycle, expecting none."
	    exit 1
	}

	uplevel #0 [linsert $mybreakcmd end $graph]
	return
    }

    proc ClearHooks {} {
	::variable myprecmd   {}
	::variable mysavecmd  {}
	::variable mybreakcmd {}
	return
    }

    # # ## ### ##### ######## #############

    proc MarkWatch {graph} {
	::variable mywatchids
	set watched [Watched $graph $mywatchids]
	if {![llength $watched]} return
	set neighbours [eval [linsert $watched 0 $graph nodes -adj]]
	#foreach n $neighbours { log write 6 cyclebreaker "Neighbor [$n id] => $n" }
	Mark $graph watched [concat $watched $neighbours]
	return
    }

    proc Watched {graph watchids} {
	set res {}
	foreach id $watchids {
	    set nl [$graph nodes -key __id__ -value $id]
	    if {![llength $nl]} continue
	    lappend res $nl
	    #log write 6 breakrcycle "Watching $id => $nl"
	    $graph node set $nl fontcolor red
	}
	return $res
    }

    # # ## ### ##### ######## #############


    typevariable myat      0 ; # Counter for commit ids for the
			       # changesets.
    typevariable mybottom {} ; # List of the candidate nodes for
			       # committing.

    typevariable myprecmd   {} ; # Callback, change graph before walk.
    typevariable mysavecmd  {} ; # Callback, for each processed node.
    typevariable mybreakcmd {} ; # Callback, for each found cycle.

    typevariable mydotdestination {} ; # Destination directory for the
				       # generated .dot files.
    typevariable mydotprefix      {} ; # Prefix for dot files when
				       # exporting the graphs.
    typevariable mydotid           0 ; # Counter for dot file name
				       # generation.
    typevariable mywatchids       {} ; # Changesets to watch the
				       # neighbourhood of.

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
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	    namespace import ::vc::fossil::import::cvs::project::revlink
	}
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::log
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::dot
	log register cyclebreaker
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::cyclebreaker 1.0
return
