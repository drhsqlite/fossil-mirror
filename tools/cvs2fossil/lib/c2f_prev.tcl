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

## Revisions per project, aka Changesets. These objects are first used
## in pass 5, which creates the initial set covering the repository.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {project cstype srcid revisions} {
	set myid        [incr mycounter]
	set myproject   $project
	set mytype      $cstype	  
	set mysrcid	$srcid	  
	set myrevisions $revisions
	return
    }

    method id {} { return $myid }

    method breakinternaldependencies {cv} {
	upvar 2 $cv csets ; # simple-dispatch!

	# This method inspects the changesets for internal
	# dependencies. Nothing is done if there are no
	# such. Otherwise the changeset is split into a set of
	# fragments without internal dependencies, transforming the
	# internal dependencies into external ones. The new changesets
	# are added to the list of all changesets.

	# Actually at most one split is performed, resulting in at
	# most one additional fragment. It is the caller's
	# responsibility to spli the resulting fragments further.

	# The code checks only sucessor dependencies, automatically
	# covering the predecessor dependencies as well (A sucessor
	# dependency a -> b is a predecessor dependency b -> a).

	# Array of dependencies (parent -> child). This is pulled from
	# the state, and limited to successors within the changeset.
	array set dependencies {}

	set theset ('[join $myrevisions {','}]')

	foreach {rid child} [state run "
	    SELECT R.rid, R.child
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.child IS NOT NULL
	    AND    R.child IN $theset
    UNION
	    SELECT R.rid, R.dbchild
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.dbchild IS NOT NULL
	    AND    R.dbchild IN $theset
    UNION
	    SELECT R.rid, B.brid
	    FROM   revision R, revisionbranchchildren B
	    WHERE  R.rid   IN $theset
	    AND    R.rid = B.rid
	    AND    B.brid IN $theset
	"] {
	    # Consider moving this to the integrity module.
	    if {$rid == $child} {
		trouble internal "Revision $rid depends on itself."
	    }
	    set dependencies($rid) $child
	}

	if {![array size dependencies]} {return 0} ; # Nothing to break.

	# We have internal dependencies to break. We now iterate over
	# all positions in the list (which is chronological, at least
	# as far as the timestamps are correct and unique) and
	# determine the best position for the break, by trying to
	# break as many dependencies as possible in one go.

	# First we create a map of positions to make it easier to
	# determine whether a dependency cross a particular index.

	array set pos {}
	array set crossing {}
	set n 0
	foreach rev $myrevisions { 
	    set pos($rev) $n
	    set crossing($n) 0
	    incr n
	}

	# Secondly we count the crossings per position, by iterating
	# over the recorded internal dependencies.

	foreach {rid child} [array get dependencies] {
	    set start $pos($rid)
	    set end $pos($child)

	    # Note: If the timestamps are badly out of order it is
	    #       possible to have a backward successor dependency,
	    #       i.e. with start > end. We may have to swap the
	    #       indices to ensure that the following loop runs
	    #       correctly.
	    #
	    # Note 2: start == end is not possible. It indicates a
	    #         self-dependency due to the uniqueness of
	    #         positions, and that is something we have ruled
	    #         out already.

	    if {$start > $end} {
		while {$end < $start} { incr crossing($end)   ; incr end }
	    } else {
		while {$start < $end} { incr crossing($start) ; incr start }
	    }
	}

	# Now we can determine the best break location. First we look
	# for the locations with the maximal number of crossings. If
	# there are several we look for the shortest time interval
	# among them. If we still have multiple possibilities after
	# that we select the smallest index among these.

	set max -1
	set best {}

	foreach key [array names crossing] {
	    set now $crossing($key)
	    if {$now > $max} {
		set max $now
		set best $key
		continue
	    } elseif {$now == $max} {
		lappend best $key
	    }
	}

	if {[llength $best] > 1} {
	    set min -1
	    set newbest {}
	    foreach at $best {
		set rat   [lindex $myrevisions $at] ; incr at
		set rnext [lindex $myrevisions $at] ; incr at -1
		set tat   [lindex [state run {SELECT R.date FROM revision R WHERE R.rid = $rat  }] 0]
		set tnext [lindex [state run {SELECT R.date FROM revision R WHERE R.rid = $rnext}] 0]
		set delta [expr {$tnext - $tat}]
		if {($min < 0) || ($delta < $min)} {
		    set min $delta
		    set newbest $at
		} elseif {$delta == $min} {
		    lappend newbest $at
		}
	    }
	    set best $newbest
	}

	if {[llength $best] > 1} {
	    set best [lindex [lsort -integer -increasing $best] 0]
	}

	# Now we can split off a fragment.

	set bnext $best ; incr bnext
	set revbefore [lrange $myrevisions 0 $best]
	set revafter  [lrange $myrevisions $bnext end]

	if {![llength $revbefore]} {
	    trouble internal "Tried to split off a zero-length fragment at the beginning"
	}
	if {![llength $revafter]} {
	    trouble internal "Tried to split off a zero-length fragment at the end"
	}

	lappend csets [set new [$type %AUTO% $myproject $mytype $mysrcid $revafter]]
	set myrevisions $revbefore

	log write 4 csets "Breaking <$myid> @$best, making <[$new id]>, cutting $crossing($best)"

	#puts "\tKeeping   <$revbefore>"
	#puts "\tSplit off <$revafter>"

	return 1
    }

    method persist {} {
	set tid $mycstype($mytype)
	set pid [$myproject id]
	set pos 0

	state transaction {
	    state run {
		INSERT INTO changeset (cid,   pid,  type, src)
		VALUES                ($myid, $pid, $tid, $mysrcid);
	    }

	    foreach rid $myrevisions {
		state run {
		    INSERT INTO csrevision (cid,   pos,  rid)
		    VALUES                 ($myid, $pos, $rid);
		}
		incr pos
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myid        ; # Id of the cset for the persistent state.
    variable myproject   ; # Reference of the project object the changeset belongs to.
    variable mytype      ; # rev or sym, where the cset originated from.
    variable mysrcid     ; # id of the metadata or symbol the cset is based on.
    variable myrevisions ; # List of the file level revisions in the cset.

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable mycounter        0 ; # Id counter for csets.
    typevariable mycstype -array {} ; # Map cstypes to persistent ids.

    typemethod getcstypes {} {
	foreach {tid name} [state run {
	    SELECT tid, name FROM cstype;
	}] { set mycstype($name) $tid }
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export rev
    namespace eval rev {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::log
	log register csets
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::rev 1.0
return
