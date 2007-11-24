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
package require vc::tools::misc                       ; # Text formatting
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {project cstype srcid revisions {theid {}}} {
	if {$theid ne ""} {
	    set myid $theid
	} else {
	    set myid [incr mycounter]
	}

	set myproject   $project
	set mytype      $cstype	  
	set mysrcid	$srcid	  
	set myrevisions $revisions
	set mypos       {} ; # Commit location is not known yet.

	# Keep track of the generated changesets and of the inverse
	# mapping from revisions to them.
	lappend mychangesets   $self
	set     myidmap($myid) $self
	foreach r $revisions { set myrevmap($r) $self }
	return
    }

    method id        {} { return $myid }
    method revisions {} { return $myrevisions }
    method data      {} { return [list $myproject $mytype $mysrcid] }

    method bysymbol   {} { return [expr {$mytype eq "sym"}] }
    method byrevision {} { return [expr {$mytype eq "rev"}] }

    method setpos {p} { set mypos $p ; return }
    method pos    {}  { return $mypos }

    method isbranch {} {
	return [expr {($mytype eq "sym") &&
		      ($mybranchcode == [state one {
			  SELECT type FROM symbol WHERE sid = $mysrcid
		      }])}]
    }

    method successormap {} {
	# NOTE / FUTURE: Possible bottleneck.
	array set tmp {}
	foreach {rev children} [$self nextmap] {
	    foreach child $children {
		lappend tmp($rev) $myrevmap($child)
	    }
	    set tmp($rev) [lsort -unique $tmp($rev)]
	}
	return [array get tmp]
    }

    method successors {} {
	# NOTE / FUTURE: Possible bottleneck.
	set csets {}
	foreach {_ children} [$self nextmap] {
	    foreach child $children {
		lappend csets $myrevmap($child)
	    }
	}
	return [lsort -unique $csets]
    }

    method predecessormap {} {
	# NOTE / FUTURE: Possible bottleneck.
	array set tmp {}
	foreach {rev children} [$self premap] {
	    foreach child $children {
		lappend tmp($rev) $myrevmap($child)
	    }
	    set tmp($rev) [lsort -unique $tmp($rev)]
	}
	return [array get tmp]
    }

    # revision -> list (revision)
    method nextmap {} {
	if {[llength $mynextmap]} { return $mynextmap }
	PullSuccessorRevisions tmp $myrevisions
	set mynextmap [array get tmp]
	return $mynextmap
    }

    # revision -> list (revision)
    method premap {} {
	if {[llength $mypremap]} { return $mypremap }
	PullPredecessorRevisions tmp $myrevisions
	set mypremap [array get tmp]
	return $mypremap
    }

    method breakinternaldependencies {} {
	# This method inspects the changesets for internal
	# dependencies. Nothing is done if there are no
	# such. Otherwise the changeset is split into a set of
	# fragments without internal dependencies, transforming the
	# internal dependencies into external ones. The new changesets
	# are added to the list of all changesets.

	# We perform all necessary splits in one go, instead of only
	# one. The previous algorithm, adapted from cvs2svn, computed
	# a lot of state which was thrown away and then computed again
	# for each of the fragments. It should be easier to update and
	# reuse that state.

	# The code checks only sucessor dependencies, as this
	# automatically covers the predecessor dependencies as well (A
	# successor dependency a -> b is also a predecessor dependency
	# b -> a).

	# Array of dependencies (parent -> child). This is pulled from
	# the state, and limited to successors within the changeset.

	array set dependencies {}
	PullInternalSuccessorRevisions dependencies $myrevisions
	if {![array size dependencies]} {return 0} ; # Nothing to break.

	log write 6 csets ...<$myid>.......................................................

	# We have internal dependencies to break. We now iterate over
	# all positions in the list (which is chronological, at least
	# as far as the timestamps are correct and unique) and
	# determine the best position for the break, by trying to
	# break as many dependencies as possible in one go. When a
	# break was found this is redone for the fragments coming and
	# after, after upding the crossing information.

	# Data structures:
	# Map:  POS   revision id      -> position in list.
	#       CROSS position in list -> number of dependencies crossing it
	#       DEPC  dependency       -> positions it crosses
	# List: RANGE Of the positions itself.
	# A dependency is a single-element map parent -> child

	InitializeBreakState $myrevisions

	set fragments {}
	set pending   [list $range]
	set at        0
	array set breaks {}

	while {$at < [llength $pending]} {
	    set current [lindex $pending $at]

	    log write 6 csets ". . .. ... ..... ........ ............."
	    log write 6 csets "Scheduled   [join [PRs [lrange $pending $at end]] { }]"
	    log write 6 csets "Considering [PR $current] \[$at/[llength $pending]\]"

	    set best [FindBestBreak $current]

	    if {$best < 0} {
		# The inspected range has no internal
		# dependencies. This is a complete fragment.
		lappend fragments $current

		log write 6 csets "No breaks, final"
	    } else {
		# Split the range and schedule the resulting fragments
		# for further inspection. Remember the number of
		# dependencies cut before we remove them from
		# consideration, for documentation later.

		set breaks($best) $cross($best)

		log write 6 csets "Best break @ $best, cutting [nsp $cross($best) dependency dependencies]"

		# Note: The value of best is an abolute location in
		# myrevisions. Use the start of current to make it an
		# index absolute to current.

		set brel [expr {$best - [lindex $current 0]}]
		set bnext $brel ; incr bnext
		set fragbefore [lrange $current 0 $brel]
		set fragafter  [lrange $current $bnext end]

		log write 6 csets "New pieces  [PR $fragbefore] [PR $fragafter]"

		if {![llength $fragbefore]} {
		    trouble internal "Tried to split off a zero-length fragment at the beginning"
		}
		if {![llength $fragafter]} {
		    trouble internal "Tried to split off a zero-length fragment at the end"
		}

		lappend pending $fragbefore $fragafter
		CutAt $best
	    }

	    incr at
	}

	log write 6 csets ". . .. ... ..... ........ ............."

	# Create changesets for the fragments, reusing the current one
	# for the first fragment. We sort them in order to allow
	# checking for gaps and nice messages.

	set fragments [lsort -index 0 -integer $fragments]

	#puts \t.[join [PRs $fragments] .\n\t.].

	Border [lindex $fragments 0] firsts firste

	if {$firsts != 0} {
	    trouble internal "Bad fragment start @ $firsts, gap, or before beginning of the range"
	}

	set laste $firste
	foreach fragment [lrange $fragments 1 end] {
	    Border $fragment s e
	    if {$laste != ($s - 1)} {
		trouble internal "Bad fragment border <$laste | $s>, gap or overlap"
	    }

	    set new [$type %AUTO% $myproject $mytype $mysrcid [lrange $myrevisions $s $e]]

            log write 4 csets "Breaking <$myid> @ $laste, new <[$new id]>, cutting $breaks($laste)"

	    set laste $e
	}

	if {$laste != ([llength $myrevisions]-1)} {
	    trouble internal "Bad fragment end @ $laste, gap, or beyond end of the range"
	}

	# Put the first fragment into the current changeset.
	set myrevisions [lrange $myrevisions 0 $firste]

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

    method timerange {} {
	set theset ('[join $myrevisions {','}]')
	return [state run "
	    SELECT MIN(R.date), MAX(R.date)
	    FROM revision R
	    WHERE R.rid IN $theset
	"]
    }

    method drop {} {
	state transaction {
	    state run {
		DELETE FROM changeset  WHERE cid = $myid;
		DELETE FROM csrevision WHERE cid = $myid;
	    }
	}
	foreach r $myrevisions { unset myrevmap($r) }
	set pos          [lsearch -exact $mychangesets $self]
	set mychangesets [lreplace $mychangesets $pos $pos]
	return
    }

    typemethod split {cset args} {
	# As part of the creation of the new changesets specified in
	# ARGS as sets of revisions, all subsets of CSET's revision
	# set, CSET will be dropped from all databases, in and out of
	# memory, and then destroyed.

	struct::list assign [$cset data] project cstype cssrc

	$cset drop
	$cset destroy

	set newcsets {}
	foreach fragmentrevisions $args {
	    if {![llength $fragmentrevisions]} {
		trouble internal "Attempted to create an empty changeset, i.e. without revisions"
	    }
	    lappend newcsets [$type %AUTO% $project $cstype $cssrc $fragmentrevisions]
	}

	foreach c $newcsets { $c persist }
	return $newcsets
    }

    # # ## ### ##### ######## #############
    ## State

    variable myid        {} ; # Id of the cset for the persistent
			      # state.
    variable myproject   {} ; # Reference of the project object the
			      # changeset belongs to.
    variable mytype      {} ; # rev or sym, where the cset originated
			      # from.
    variable mysrcid     {} ; # Id of the metadata or symbol the cset
			      # is based on.
    variable myrevisions {} ; # List of the file level revisions in
			      # the cset.
    variable mypremap    {} ; # Dictionary mapping from the revisions
			      # to their predecessors. Cache to avoid
			      # loading this from the state more than
			      # once.
    variable mynextmap   {} ; # Dictionary mapping from the revisions
			      # to their successors. Cache to avoid
			      # loading this from the state more than
			      # once.
    variable mypos       {} ; # Commit position of the changeset, if
			      # known.

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable mycounter        0 ; # Id counter for csets. Last id used.
    typevariable mycstype -array {} ; # Map cstypes to persistent ids.

    typemethod getcstypes {} {
	foreach {tid name} [state run {
	    SELECT tid, name FROM cstype;
	}] { set mycstype($name) $tid }
	return
    }

    typemethod loadcounter {} {
	# Initialize the counter from the state
	set mycounter [state one { SELECT MAX(cid) FROM changeset }]
	return
    }

    proc PullInternalSuccessorRevisions {dv revisions} {
	upvar 1 $dv dependencies
	set theset ('[join $revisions {','}]')

	foreach {rid child} [state run "
   -- Primary children
	    SELECT R.rid, R.child
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.child IS NOT NULL
	    AND    R.child IN $theset
    UNION
    -- Transition NTDB to trunk
	    SELECT R.rid, R.dbchild
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.dbchild IS NOT NULL
	    AND    R.dbchild IN $theset
    UNION
    -- Secondary (branch) children
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
	    lappend dependencies($rid) $child
	}
    }

    proc PullSuccessorRevisions {dv revisions} {
	upvar 1 $dv dependencies
	set theset ('[join $revisions {','}]')

	foreach {rid child} [state run "
   -- Primary children
	    SELECT R.rid, R.child
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.child IS NOT NULL
    UNION
    -- Transition NTDB to trunk
	    SELECT R.rid, R.dbchild
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.dbchild IS NOT NULL
    UNION
    -- Secondary (branch) children
	    SELECT R.rid, B.brid
	    FROM   revision R, revisionbranchchildren B
	    WHERE  R.rid   IN $theset
	    AND    R.rid = B.rid
	"] {
	    # Consider moving this to the integrity module.
	    if {$rid == $child} {
		trouble internal "Revision $rid depends on itself."
	    }
	    lappend dependencies($rid) $child
	}
	return
    }

    proc PullPredecessorRevisions {dv revisions} {
	upvar 1 $dv dependencies
	set theset ('[join $revisions {','}]')

	foreach {rid parent} [state run "
   -- Primary parent, can be in different LOD for first in a branch
	    SELECT R.rid, R.parent
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.parent IS NOT NULL
    UNION
    -- Transition trunk to NTDB
	    SELECT R.rid, R.dbparent
	    FROM   revision R
	    WHERE  R.rid   IN $theset
	    AND    R.dbparent IS NOT NULL
	"] {
	    # Consider moving this to the integrity module.
	    if {$rid == $parent} {
		trouble internal "Revision $rid depends on itself."
	    }
	    lappend dependencies($rid) $parent
	}
	return
    }

    proc InitializeBreakState {revisions} {
	upvar 1 pos pos cross cross range range depc depc delta delta \
	    dependencies dependencies

	# First we create a map of positions to make it easier to
	# determine whether a dependency crosses a particular index.

	array set pos   {}
	array set cross {}
	array set depc  {}
	set range       {}
	set n 0
	foreach rev $revisions { 
	    lappend range $n
	    set pos($rev) $n
	    set cross($n) 0
	    incr n
	}

	# Secondly we count the crossings per position, by iterating
	# over the recorded internal dependencies.

	# Note: If the timestamps are badly out of order it is
	#       possible to have a backward successor dependency,
	#       i.e. with start > end. We may have to swap the indices
	#       to ensure that the following loop runs correctly.
	#
	# Note 2: start == end is not possible. It indicates a
	#         self-dependency due to the uniqueness of positions,
	#         and that is something we have ruled out already, see
	#         PullInternalSuccessorRevisions.

	foreach {rid child} [array get dependencies] {
	    set dkey    [list $rid $child]
	    set start   $pos($rid)
	    set end     $pos($child)
	    set crosses {}

	    if {$start > $end} {
		while {$end < $start} {
		    lappend crosses $end
		    incr cross($end)
		    incr end
		}
	    } else {
		while {$start < $end} {
		    lappend crosses $start
		    incr cross($start)
		    incr start
		}
	    }
	    set depc($dkey) $crosses
	}

	InitializeDeltas $revisions
	return
    }

    proc InitializeDeltas {revisions} {
	upvar 1 delta delta

	# Pull the timestamps for all revisions in the changesets and
	# compute their deltas for use by the break finder.

	array set delta {}
	array set stamp {}

	set theset ('[join $revisions {','}]')
	foreach {rid time} [state run "
	    SELECT R.rid, R.date
	    FROM revision R
	    WHERE R.rid IN $theset
	"] {
	    set stamp($rid) $time
	}

	set n 0
	foreach rid [lrange $revisions 0 end-1] rnext [lrange $revisions 1 end] {
	    set delta($n) [expr {$stamp($rnext) - $stamp($rid)}]
	    incr n
	}
	return
    }

    proc FindBestBreak {range} {
	upvar 1 cross cross delta delta

	# Determine the best break location in the given range of
	# positions. First we look for the locations with the maximal
	# number of crossings. If there are several we look for the
	# shortest time interval among them. If we still have multiple
	# possibilities after that we select the earliest location
	# among these.

	# Note: If the maximal number of crossings is 0 then the range
	#       has no internal dependencies, and no break location at
	#       all. This possibility is signaled via result -1.

	# Note: A range of length 1 or less cannot have internal
	#       dependencies, as that needs at least two revisions in
	#       the range.

	if {[llength $range] < 2} { return -1 }

	set max -1
	set best {}

	foreach location $range {
	    set crossings $cross($location)
	    if {$crossings > $max} {
		set max  $crossings
		set best [list $location]
		continue
	    } elseif {$crossings == $max} {
		lappend best $location
	    }
	}

	if {$max == 0}            { return -1 }
	if {[llength $best] == 1} { return [lindex $best 0] }

	set locations $best
	set best {}
	set min -1

	foreach location $locations {
	    set interval $delta($location)
	    if {($min < 0) || ($interval < $min)} {
		set min  $interval
		set best [list $location]
	    } elseif {$interval == $min} {
		lappend best $location
	    }
	}

	if {[llength $best] == 1} { return [lindex $best 0] }

	return [lindex [lsort -integer -increasing $best] 0]
    }

    proc CutAt {location} {
	upvar 1 cross cross depc depc

	# It was decided to split the changeset at the given
	# location. This cuts a number of dependencies. Here we update
	# the cross information so that the break finder has accurate
	# data when we look at the generated fragments.

	set six [log visible? 6]

	foreach {dep range} [array get depc] {
	    # Check all dependencies still known, take their range and
	    # see if the break location falls within.

	    Border $range s e
	    if {$location < $s} continue ; # break before range, ignore
	    if {$location > $e} continue ; # break after range, ignore.

	    # This dependency crosses the break location. We remove it
	    # from the crossings counters, and then also from the set
	    # of known dependencies, as we are done with it.

	    foreach loc $depc($dep) { incr cross($loc) -1 }
	    unset depc($dep)

	    if {!$six} continue

	    struct::list assign $dep parent child
	    log write 6 csets "Broke dependency [PD $parent] --> [PD $child]"
	}

	return
    }

    # Print identifying data for a revision (project, file, dotted rev
    # number), for high verbosity log output.

    proc PD {id} {
	foreach {p f r} [state run {
		SELECT P.name , F.name, R.rev
		FROM revision R, file F, project P
		WHERE R.rid = $id
		AND   R.fid = F.fid
		AND   F.pid = P.pid
	}] break
	return "'$p : $f/$r'"
    }

    # Printing one or more ranges, formatted, and only their border to
    # keep the strings short.

    proc PRs {ranges} {
	return [struct::list map $ranges [myproc PR]]
    }

    proc PR {range} {
	Border $range s e
	return <${s}...${e}>
    }

    proc Border {range sv ev} {
	upvar 1 $sv s $ev e
	set s [lindex $range 0]
	set e [lindex $range end]
	return
    }

    # # ## ### ##### ######## #############

    typevariable mychangesets    {} ; # List of all known changesets.
    typevariable myrevmap -array {} ; # Map from revisions to their changeset.
    typevariable myidmap  -array {} ; # Map from changeset id to changeset.
    typevariable mybranchcode    {} ; # Local copy of project::sym/mybranch.

    typemethod all   {}   { return $mychangesets }
    typemethod of    {id} { return $myidmap($id) }
    typemethod ofrev {id} { return $myrevmap($id) }

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
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	::variable mybranchcode [project::sym branch]
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register csets
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::rev 1.0
return
