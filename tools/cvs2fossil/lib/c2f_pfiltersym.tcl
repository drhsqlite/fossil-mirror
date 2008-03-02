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

## Pass IV. Coming after the symbol collation pass this pass now
## removes all revisions and symbols referencing any of the excluded
## symbols from the persistent database.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::misc                       ; # Text formatting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::integrity    ; # State storage integrity checks.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    FilterSymbols \
    {Filter symbols, remove all excluded pieces} \
    ::vc::fossil::import::cvs::pass::filtersym

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::filtersym {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define names and structure of the persistent state of this
	# pass.

	state use project
	state use file
	state use revision
	state use revisionbranchchildren
	state use branch
	state use tag
	state use symbol
	state use blocker
	state use parent
	state use author
	state use cmessage
	state use preferedparent

	# NOTE: So far no pass coming after this makes us of this information.
	state extend noop {
	    id    INTEGER NOT NULL  PRIMARY KEY, -- tag/branch reference
	    noop  INTEGER NOT NULL
	}
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	# The results of this pass are fully in the persistent state,
	# there is nothing to load for the next one.
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	# The removal of excluded symbols and everything referencing
	# to them is done completely in the database.

	state transaction {
	    FilterExcludedSymbols
	    MutateSymbols
	    AdjustParents
	    RefineSymbols

	    PrintSymbolTree
	    repository printrevstatistics

	    # Strict integrity enforces that all meta entries are in
	    # the same LOD as the revision using them. At this point
	    # this may not be true any longer. If a NTDB was excluded
	    # then all revisions it shared with the trunk were moved
	    # to the trunk LOD, however their meta entries will still
	    # refer to the now gone LOD symbol. This is fine however,
	    # it will not affect our ability to use the meta entries
	    # to distinguish and group revisions into changesets. It
	    # should be noted that we cannot simply switch the meta
	    # entries over to the trunk either, as that may cause the
	    # modified entries to violate the unique-ness constraint
	    # set on that table.
	    integrity metarelaxed
	}

	log write 1 filtersym "Filtering completed"
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc FilterExcludedSymbols {} {
	log write 3 filtersym "Remove the excluded symbols and their users"

	# We pull all the excluded symbols together into a table for
	# easy reference by the upcoming DELETE and other statements.
	# ('x IN table' clauses).

	set excl [project::sym excluded]

	state run {
	    CREATE TEMPORARY TABLE excludedsymbols AS
	    SELECT sid
	    FROM   symbol
	    WHERE  type = $excl
	}

	# First we have to handle the possibility of an excluded
	# NTDB. This is a special special case there we have to
	# regraft the revisions which are shared between the NTDB and
	# Trunk onto the trunk, preventing their deletion later. We
	# have code for that in 'file', however that operated on the
	# in-memory revision objects, which we do not have here. We do
	# the same now without object, by directly manipulating the
	# links in the database.

	array set ntdb {}
	array set link {}

	state foreachrow {
	    SELECT R.rid     AS xid,
	           R.parent  AS xparent,
	           R.dbchild AS transfer
	    FROM  revision R, symbol S
	    WHERE R.lod = S.sid            -- Get symbol of line-of-development of all revisions
	    AND   S.sid IN excludedsymbols -- Restrict to the excluded symbols
	    AND   R.isdefault              -- Restrict to NTDB revisions
	} {
	    set ntdb($xid) $xparent
	    if {$transfer eq ""} continue
	    set link($xid) $transfer
	}

	foreach joint [array names link] {
	    # The joints are the highest NTDB revisions which are
	    # shared with their respective trunk. We disconnect from
	    # their NTDB children, and make them parents of their
	    # 'dbchild'. The associated 'dbparent' is squashed
	    # instead. All parents of the joints are moved to the
	    # trunk as well.

	    set tjoint $link($joint)
	    set tlod [state one {
		SELECT lod FROM revision WHERE rid = $tjoint
	    }]

	    # Covnert db/parent/child into regular parent/child links.
	    state run {
		UPDATE revision SET dbparent = NULL, parent = $joint  WHERE rid = $tjoint ;
		UPDATE revision SET dbchild  = NULL, child  = $tjoint WHERE rid = $joint  ;
	    }
	    while {1} {
		# Move the NTDB trunk revisions to trunk.
		state run {
		    UPDATE revision SET lod = $tlod, isdefault = 0 WHERE rid = $joint
		}
		set last $joint
		set joint $ntdb($joint)
		if {![info exists ntdb($joint)]} break
	    }

	    # Reached the NTDB basis in the trunk. Finalize the
	    # parent/child linkage and squash the branch parent symbol
	    # reference.

	    state run {
		UPDATE revision SET child   = $last WHERE rid = $joint ;
		UPDATE revision SET bparent = NULL  WHERE rid = $last  ;
	    }
	}

	# Now that the special case is done we can simply kill all the
	# revisions, tags, and branches referencing any of the
	# excluded symbols in some way. This is easy as we do not have
	# to select them again and again from the base tables any
	# longer.

	state run {
	    CREATE TEMPORARY TABLE excludedrevisions AS
	    SELECT rid FROM revision WHERE lod IN excludedsymbols;

	    DELETE FROM revision WHERE lod IN excludedsymbols;
	    DELETE FROM tag      WHERE lod IN excludedsymbols;
	    DELETE FROM tag      WHERE sid IN excludedsymbols;
	    DELETE FROM branch   WHERE lod IN excludedsymbols;
	    DELETE FROM branch   WHERE sid IN excludedsymbols;

	    DELETE FROM revisionbranchchildren WHERE rid  IN excludedrevisions;
	    DELETE FROM revisionbranchchildren WHERE brid IN excludedrevisions;
	    DELETE FROM blob                   WHERE rid  IN excludedrevisions;

	    DROP TABLE excludedrevisions;
	    DROP TABLE excludedsymbols;
	}
	return
    }

    proc MutateSymbols {} {
	# Next, now that we know which symbols are what we look for
	# file level tags which are actually converted as branches
	# (project level, and vice versa), and move them to the
	# correct tables.

	# # ## ### ##### ######## #############

	log write 3 filtersym "Mutate symbols, preparation"

	set branch [project::sym branch]
	set tag    [project::sym tag]

	set tagstomutate [state run {
	    SELECT T.tid, T.fid, T.lod, T.sid, T.rev
	    FROM tag T, symbol S
	    WHERE T.sid = S.sid
	    AND S.type = $branch
	}]

	set branchestomutate [state run {
	    SELECT B.bid, B.fid, B.lod, B.sid, B.root, B.first, B.bra
	    FROM branch B, symbol S
	    WHERE B.sid = S.sid
	    AND S.type = $tag
	}]

	set nt [expr {[llength $tagstomutate]/5}]
	set nb [expr {[llength $branchestomutate]/7}]

	log write 4 filtersym "Changing [nsp $nt tag] into  [nsp $nt branch branches]"
	log write 4 filtersym "Changing [nsp $nb branch branches] into  [nsp $nb tag]"

	# # ## ### ##### ######## #############

	log write 3 filtersym "Mutate tags to branches"

	foreach {id fid lod sid rev} $tagstomutate {
	    state run {
		DELETE FROM tag WHERE tid = $id ;
		INSERT INTO branch (bid, fid,  lod,  sid,  root, first, bra, pos)
		VALUES             ($id, $fid, $lod, $sid, $rev, NULL,  '',  -1);
	    }
	}

	log write 3 filtersym "Ok."

	# # ## ### ##### ######## #############

	log write 3 filtersym "Mutate branches to tags"

	foreach {id fid lod sid root first bra} $branchestomutate {
	    state run {
		DELETE FROM branch WHERE bid = $id ;
		INSERT INTO tag (tid, fid,  lod,  sid,  rev)
		VALUES          ($id, $fid, $lod, $sid, $root);
	    }
	}

	log write 3 filtersym "Ok."

	# # ## ### ##### ######## #############
	return
    }

    # Adjust the parents of symbols to their preferred parents.

    # If a file level ymbol has a preferred parent that is different
    # than its current parent, and if the preferred parent is an
    # allowed parent of the symbol in this file, then we graft the
    # aSymbol onto its preferred parent.

    proc AdjustParents {} {
	log write 3 filtersym "Adjust parents, loading data in preparation"

	# We pull important maps once into memory so that we do quick
	# hash lookup later when processing the graft candidates.

	# Tag/Branch names ...
	array set sn [state run { SELECT T.tid, S.name FROM tag T,    symbol S WHERE T.sid = S.sid }]
	array set sn [state run { SELECT B.bid, S.name FROM branch B, symbol S WHERE B.sid = S.sid }]
	# Symbol names ...
	array set sx [state run { SELECT L.sid, L.name FROM symbol L }]
	# Files and projects.
	array set fpn {}
	state foreachrow {
		SELECT F.fid AS id, F.name AS fn, P.name AS pn
		FROM   file F, project P
		WHERE  F.pid = P.pid
	} { set fpn($id) [list $fn $pn] }

	set tagstoadjust [state run {
	    SELECT T.tid, T.fid, T.lod, P.pid, S.name, R.rev, R.rid
	    FROM tag T, preferedparent P, symbol S, revision R
	    WHERE T.sid = P.sid        -- For all tags, get left-hand of prefered parent via symbol
	    AND   T.lod != P.pid       -- Restrict to tags whose LOD is not their prefered parent
	    AND   P.pid = S.sid        -- Get symbol of prefered parent
	    AND   S.name != ':trunk:'  -- Exclude trunk parentage
	    AND   T.rev = R.rid        -- Get revision the tag is attached to.
	}]

	set branchestoadjust [state run {
	    SELECT B.bid, B.fid, B.lod, B.pos, P.pid, S.name, NULL, NULL
	    FROM branch B, preferedparent P, symbol S
	    WHERE B.sid = P.sid        -- For all branches, get left-hand of prefered parent via symbol
	    AND   B.lod != P.pid       -- Restrict to branches whose LOD is not their prefered parent
	    AND   P.pid = S.sid        -- Get symbol of prefered parent
	    AND   S.name != ':trunk:'  -- Exclude trunk parentage
	    AND   B.root IS NULL       -- Accept free-floating branch
    UNION
	    SELECT B.bid, B.fid, B.lod, B.pos, P.pid, S.name, R.rev, R.rid
	    FROM branch B, preferedparent P, symbol S, revision R
	    WHERE B.sid = P.sid        -- For all branches, get left-hand of prefered parent via symbol
	    AND   B.lod != P.pid       -- Restrict to branches whose LOD is not their prefered parent
	    AND   P.pid = S.sid	       -- Get symbol of prefered parent
	    AND   S.name != ':trunk:'  -- Exclude trunk parentage
	    AND   B.root = R.rid       -- Get root revision of the branch
	}]

	set tmax [expr {[llength $tagstoadjust] / 7}]
	set bmax [expr {[llength $branchestoadjust] / 8}]

	log write 4 filtersym "Reparenting at most [nsp $tmax tag]"
	log write 4 filtersym "Reparenting at most [nsp $bmax branch branches]"

	log write 3 filtersym "Adjust tag parents"

	# Find the tags whose current parent (lod) is not the prefered
	# parent, the prefered parent is not the trunk, and the
	# prefered parent is a possible parent per the tag's revision.

	set fmt %[string length $tmax]s
	set mxs [format $fmt $tmax]

	set n 0
	foreach {id fid lod pid preferedname revnr rid} $tagstoadjust {
	    # BOTTLE-NECK ...
	    #
	    # The check if the candidate (pid) is truly viable is
	    # based on finding the branch as possible parent, and done
	    # now instead of as part of the already complex join.
	    #
	    # ... AND P.pid IN (SELECT B.sid
	    #                   FROM branch B
	    #                   WHERE B.root = R.rid)

	    if {![state one {
		SELECT COUNT(*)       -- Count <=> Check existence.
		FROM branch B
		WHERE  B.sid  = $pid  -- Restrict to branch for that symbol
		AND    B.root = $rid  -- attached to that revision
	    }]} {
		incr tmax -1
		set  mxs [format $fmt $tmax]
		continue
	    }

	    #
	    # BOTTLE-NECK ...

	    # The names for use in the log output are retrieved
	    # separately, to keep the join selecting the adjustable
	    # tags small, not burdened with the dereferencing of links
	    # to name.

	    set tagname $sn($id)
	    set oldname $sx($lod)
	    struct::list assign $fpn($fid) fname prname

	    # Do the grafting.

	    log write 4 filtersym {\[[format $fmt $n]/$mxs\] $prname : Grafting tag '$tagname' on $fname/$revnr from '$oldname' onto '$preferedname'}
	    state run { UPDATE tag SET lod = $pid WHERE tid = $id }
	    incr n
	}

	log write 3 filtersym "Reparented [nsp $n tag]"

	log write 3 filtersym "Adjust branch parents"

	# Find the branches whose current parent (lod) is not the
	# prefered parent, the prefered parent is not the trunk, and
	# the prefered parent is a possible parent per the branch's
	# revision.

	set fmt %[string length $bmax]s
	set mxs [format $fmt $bmax]

	set n 0
	foreach {id fid lod pos pid preferedname revnr rid} $branchestoadjust {

	    # BOTTLE-NECK ...
	    #
	    # The check if the candidate (pid) is truly viable is
	    # based on the branch positions in the spawning revision,
	    # and done now instead of as part of the already complex
	    # join.
	    #
	    # ... AND P.pid IN (SELECT BX.sid
	    #                   FROM branch BX
	    #                   WHERE BX.root = R.rid
	    #                   AND   BX.pos > B.pos)

	    # Note: rid eq "" hear means that this is a free-floating
	    # branch, whose original root was removed as a unnecessary
	    # dead revision (See 'file::RemoveIrrelevantDeletions').
	    # Such a branch can be regrafted without trouble and there
	    # is no need to look for the new parent in its
	    # non-existent root.

	    if {($rid ne "") && ![state one {
		SELECT COUNT(*)       -- Count <=> Check existence.
		FROM branch B
		WHERE  B.sid  = $pid  -- Look for branch by symbol
		AND    B.root = $rid  -- Spawned by the given revision
		AND    B.pos  > $pos  -- And defined before branch to mutate
	    }]} {
		incr bmax -1
		set  mxs [format $fmt $bmax]
		continue
	    }

	    #
	    # BOTTLE-NECK ...

	    # The names for use in the log output are retrieved
	    # separately, to keep the join selecting the adjustable
	    # tags small, not burdened with the dereferencing of links
	    # to name.

	    set braname $sn($id)
	    set oldname $sx($lod)
	    struct::list assign $fpn($fid) fname prname

	    # Do the grafting.

	    log write 4 filtersym {\[[format $fmt $n]/$mxs\] $prname : Grafting branch '$braname' on $fname/$revnr from '$oldname' onto '$preferedname'}
	    state run { UPDATE branch SET lod = $pid WHERE bid = $id }
	    incr n
	}

	log write 3 filtersym "Reparented [nsp $n branch branches]"
	return
    }

    proc RefineSymbols {} {
	# Tags and branches are marked as normal/noop based on the op
	# of their revision.

	log write 3 filtersym "Refine symbols (no-op or not?)"

	# Note: The noop information is not used anywhere. Consider
	# disabling this code, and removing the data from the state.

	log write 4 filtersym "    Regular tags"
	state run {
	    INSERT INTO noop
	    SELECT T.tid, 0
	    FROM tag T, revision R
	    WHERE T.rev  = R.rid
	    AND   R.op  != 0 -- 0 == nothing
	}

	log write 4 filtersym "    No-op tags"
	state run {
	    INSERT INTO noop
	    SELECT T.tid, 1
	    FROM tag T, revision R
	    WHERE T.rev  = R.rid
	    AND   R.op   = 0 -- nothing
	}

	log write 4 filtersym "    Regular branches"
	state run {
	    INSERT INTO noop
	    SELECT B.bid, 0
	    FROM branch B, revision R
	    WHERE B.root = R.rid
	    AND   R.op  != 0 -- nothing
	}

	log write 4 filtersym "    No-op branches"
	state run {
	    INSERT INTO noop
	    SELECT B.bid, 1
	    FROM branch B, revision R
	    WHERE B.root = R.rid
	    AND   R.op   = 0 -- nothing
	}
	return
    }

    proc maxlen {v str} {
	upvar 1 $v n
	set l [string length $str]
	if {$l <= $n} return
	set n $l
	return
    }

    proc PrintSymbolTree {} {
	if {![log visible? 9]} return

	array set sym {}
	set n 0
	set t 0
	set c 0
	set p 0

	state foreachrow {
	    SELECT S.name         AS xs,
	           A.name         AS stype,
	           S.commit_count AS cc,
	           P.name         AS xp,
	           B.name         AS ptype
	    FROM symbol S, preferedparent SP, symbol P, symtype A, symtype B
	    WHERE SP.sid = S.sid
	    AND   P.sid = SP.pid
	    AND   A.tid = S.type
	    AND   B.tid = P.type
	} {
	    lappend sym($xs) $xp $stype $ptype $cc
	    maxlen n $xs
	    maxlen t $stype
	    maxlen t $ptype
	    maxlen c $cc
	    maxlen p $xp
	}

	foreach xs [lsort -dict [array names sym]] {
	    struct::list assign $sym($xs) xp stype ptype cc
	    log write 9 filtersym {Tree: [lj $t $stype] ([dj $c $cc]) [lj $n $xs] <-- [lj $t $ptype] $xp}
	}
	return
    }

    proc lj {n s} { ::format %-${n}s $s }
    proc dj {n s} { ::format %-${n}d $s }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export filtersym
    namespace eval filtersym {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	namespace import ::vc::tools::misc::nsp
	namespace import ::vc::tools::log
	log register filtersym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::filtersym 1.0
return
