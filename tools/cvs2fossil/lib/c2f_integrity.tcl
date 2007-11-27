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

## This package holds a number of integrity checks done on the
## persistent state. This is used by the passes II and IV.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::integrity {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod strict {} {
	log write 4 integrity {Check database consistency}

	set n 0
	AllButMeta
	Meta
	return
    }

    typemethod metarelaxed {} {
	log write 4 integrity {Check database consistency}

	set n 0
	AllButMeta
	return
    }

    typemethod changesets {} {
	log write 4 integrity {Check database consistency}

	set n 0
	AllChangesets
	RevisionChangesets
	SymbolChangesets
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc AllButMeta {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent cross-references.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all revisions which disagree with their line of
	# development about the project they are owned by.
	Check \
	    {Revisions and their LODs have to be in the same project} \
	    {disagrees with its LOD about owning project} {
		SELECT F.name, R.rev
		FROM revision R, file F, symbol S
		WHERE R.fid = F.fid
		AND   R.lod = S.sid
		AND   F.pid != S.pid
		;
	    }
	# Find all revisions which disgree with their meta data about
	# the project they are owned by.
	Check \
	    {Revisions and their meta data have to be in the same project} \
	    {disagrees with its meta data about owning project} {
		SELECT F.name, R.rev
		FROM revision R, file F, meta M
		WHERE R.fid = F.fid
		AND   R.mid = M.mid
		AND   F.pid != M.pid
		;
	    }
	# Find all revisions with a primary child which disagrees
	# about the file they belong to.
	Check \
	    {Revisions and their primary children have to be in the same file} \
	    {disagrees with its primary child about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.child IS NOT NULL
		AND   R.child = C.rid
		AND   C.fid != R.fid
		;
	    }

	# Find all revisions with a branch parent symbol whose parent
	# disagrees about the file they belong to.
	Check \
	    {Revisions and their branch children have to be in the same file} \
	    {at the beginning of its branch and its parent disagree about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NOT NULL
		AND   R.parent = P.rid
		AND   R.fid != P.fid
		;
	    }
	# Find all revisions with a non-NTDB child which disagrees
	# about the file they belong to.
	Check \
	    {Revisions and their non-NTDB children have to be in the same file} \
	    {disagrees with its non-NTDB child about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.dbchild IS NOT NULL
		AND   R.dbchild = C.rid
		AND   C.fid != R.fid
		;
	    }
	# Find all revisions which have a primary child, but the child
	# does not have them as parent.
	Check \
	    {Revisions have to be parents of their primary children} \
	    {is not the parent of its primary child} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.child IS NOT NULL
		AND   R.child = C.rid
		AND   C.parent != R.rid
		;
	    }
	# Find all revisions which have a primrary child, but the
	# child has a branch parent symbol making them brach starters.
	Check \
	    {Primary children of revisions must not start branches} \
	    {is parent of a primary child which is the beginning of a branch} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.child IS NOT NULL
		AND   R.child = C.rid
		AND   C.bparent IS NOT NULL
		;
	    }
	# Find all revisions without branch parent symbol which have a
	# parent, but the parent does not have them as primary child.
	Check \
	    {Revisions have to be primary children of their parents, if any} \
	    {is not the child of its parent} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NULL
		AND   R.parent IS NOT NULL
		AND   R.parent = P.rid
		AND   P.child != R.rid
		;
	    }
	# Find all revisions with a branch parent symbol which do not
	# have a parent.
	Check \
	    {Branch starting revisions have to have a parent} \
	    {at the beginning of its branch has no parent} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NOT NULL
		AND   R.parent IS NULL
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# has them as primary child.
	Check \
	    {Branch starting revisions must not be primary children of their parents} \
	    {at the beginning of its branch is the primary child of its parent} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NOT NULL
		AND   R.parent IS NOT NULL
		AND   R.parent = P.rid
		AND   P.child = R.rid
		;
	    }
	# Find all revisions with a non-NTDB child which are not on
	# the NTDB.
	Check \
	    {NTDB to trunk transition has to begin on NTDB} \
	    {has a non-NTDB child, yet is not on the NTDB} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid
		AND   R.dbchild IS NOT NULL
		AND   NOT R.isdefault
		;
	    }
	# Find all revisions with a NTDB parent which are on the NTDB.
	Check \
	    {NTDB to trunk transition has to end on non-NTDB} \
	    {has a NTDB parent, yet is on the NTDB} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid
		AND   R.dbparent IS NOT NULL
		AND   R.isdefault
		;
	    }
	# Find all revisions with a child which disagrees about the
	# line of development they belong to.
	Check \
	    {Revisions and their primary children have to be in the same LOD} \
	    {and its primary child disagree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.child IS NOT NULL
		AND   R.child = C.rid
		AND   C.lod != R.lod
		;
	    }
	# Find all revisions with a non-NTDB child which agrees about
	# the line of development they belong to.
	Check \
	    {NTDB and trunk revisions have to be in different LODs} \
	    {on NTDB and its non-NTDB child wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid
		AND   R.dbchild IS NOT NULL
		AND   R.dbchild = C.rid
		AND   C.lod = R.lod
		;
	    }
	# Find all revisions with a branch parent symbol which is not
	# their LOD.
	Check \
	    {Branch starting revisions have to have their LOD as branch parent symbol} \
	    {at the beginning of its branch does not have the branch symbol as its LOD} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NOT NULL
		AND   R.lod != R.bparent
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# is in the same line of development.
	Check \
	    {Revisions and their branch children have to be in different LODs} \
	    {at the beginning of its branch and its parent wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid
		AND   R.bparent IS NOT NULL
		AND   R.parent = P.rid
		AND   R.lod = P.lod
		;
	    }
	return
    }

    proc Meta {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent cross-references.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all revisions which disgree with their meta data about
	# the branch/line of development they belong to.
	Check \
	    {Revisions and their meta data have to be in the same LOD} \
	    {disagrees with its meta data about owning LOD} {
		SELECT F.name, R.rev
		FROM revision R, meta M, file F
		WHERE R.mid = M.mid
		AND   R.lod != M.bid
		AND   R.fid = F.fid
		;
	    }
	return
    }

    proc AllChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all revisions which are not used by at least one
	# revision changeset.
	Check \
	    {All revisions have to be used by least one revision changeset} \
	    {is not used by a revision changeset} {
		-- Unused revisions = All revisions
		--                  - revisions used by revision changesets.
		--
		-- Both sets can be computed easily, and subtracted
                -- from each other. Then we can get the associated
                -- file (name) for display.

		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.rid IN (SELECT rid FROM revision                      -- All revisions
				EXCEPT                                     -- subtract
				SELECT CR.rid FROM csrevision CR, changeset C -- revisions used
				WHERE C.cid = CR.cid                          -- by any revision
				AND C.type = 0)                               -- changeset
		AND   R.fid = F.fid              -- get file of unused revision
	    }
	# Find all revisions which are used by more than one revision
	# changeset.
	Check \
	    {All revisions have to be used by at most one revision changeset} \
	    {is used by multiple revision changesets} {
		-- Principle of operation: Get all revision/changeset
                -- pairs for all revision changesets, group by
                -- revision to aggregate the changeset, counting
                -- them. From the resulting revision/count table
                -- select those with more than one user, and get their
                -- associated file (name) for display.

		SELECT F.name, R.rev
		FROM revision R, file F,
		     (SELECT CR.rid AS rid, count(CR.cid) AS count
		      FROM csrevision CR, changeset C
		      WHERE C.type = 0
		      AND   C.cid = CR.cid
		      GROUP BY CR.rid ) AS U
		WHERE U.count > 1
		AND R.rid = U.rid
		AND R.fid = F.fid
	    }
	# All revisions in all changesets have to agree on the LOD
	# their changeset belongs to. In other words, all revisions in
	# a changeset have to refer to the same line of development.
	#
	# Instead of looking at all pairs of revisions in all
	# changesets we generate the distinct set of all LODs
	# referenced by the revisions of a changeset, look for those
	# with cardinality > 1, and get the identifying information
	# for the changesets found thusly.
	CheckCS \
	    {All revisions in a changeset have to belong to the same LOD} \
	    {: Its revisions disagree about the LOD they belong to} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT U.cid
				 FROM (SELECT DISTINCT CR.cid AS cid, R.lod AS lod
				       FROM   csrevision CR, revision R
				       WHERE  CR.rid = R.rid) AS U
				 GROUP BY U.cid HAVING COUNT(U.lod) > 1)
		AND    T.tid = C.type
	    }
	# All revisions in all changesets have to agree on the project
	# their changeset belongs to. In other words, all revisions in
	# a changeset have to refer to the same project.
	#
	# Instead of looking at all pairs of revisions in all
	# changesets we generate the distinct set of all projects
	# referenced by the revisions of a changeset, look for those
	# with cardinality > 1, and get the identifying information
	# for the changesets found thusly.
	CheckCS \
	    {All revisions in a changeset have to belong to the same project} \
	    {: Its revisions disagree about the project they belong to} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT U.cid
				 FROM (SELECT DISTINCT CR.cid AS cid, F.pid AS pid
				       FROM   csrevision CR, revision R, file F
				       WHERE  CR.rid = R.rid
				       AND    F.fid  = R.fid) AS U
				 GROUP BY U.cid HAVING COUNT(U.pid) > 1)
		AND    T.tid = C.type
	    }
	# All revisions in a single changeset have to belong to
	# different files. Conversely: No two revisions of a single
	# file are allowed to be in the same changeset.
	#
	# Instead of looking at all pairs of revisions in all
	# changesets we generate the distinct set of all files
	# referenced by the revisions of a changeset, and look for
	# those with cardinality < the cardinality of the set of
	# revisions, and get the identifying information for the
	# changesets found thusly.
	CheckCS \
	    {All revisions in a changeset have to belong to different files} \
	    {: Its revisions share files} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT VV.cid
				 FROM (SELECT U.cid as cid, COUNT (U.fid) AS fcount
				       FROM (SELECT DISTINCT CR.cid AS cid, R.fid AS fid
					     FROM   csrevision CR, revision R
					     WHERE  CR.rid = R.rid) AS U
				       GROUP BY U.cid) AS UU,
				      (SELECT V.cid AS cid, COUNT (V.rid) AS rcount
				       FROM csrevision V
				       GROUP BY V.cid) AS VV
				 WHERE VV.cid = UU.cid
				 AND   UU.fcount < VV.rcount)
		AND    T.tid = C.type
	    }
	return
    }

    proc RevisionChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# All revisions used by revision changesets have to refer to
	# the same meta information as their changeset.
	CheckInCS \
	    {All revisions have to agree with their revision changeset about the used meta information} \
	    {disagrees with its revision changeset @ about the meta information} {
		SELECT CT.name, C.cid, F.name, R.rev
		FROM changeset C, cstype CT, revision R, file F, csrevision CR
		WHERE C.type = 0       -- revision changesets only
		AND   C.cid  = CR.cid  -- changeset --> its revisions
		AND   R.rid  = CR.rid  -- look at them
		AND   R.mid != C.src   -- Only those which disagree with changeset about the meta
		AND   R.fid = F.fid    -- get file of the revision
		AND   CT.tid = C.type  -- get changeset type, for labeling
	    }
	return
    }

    proc SymbolChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	return ; # Disabled for now, bottlenecks ...

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# The next two checks are BOTTLENECKS. In essence we are
	# checking each symbol changeset one by one.

	# TODO: Try to rephrase the checks to make more use of
	# indices, set and stream operations.

	# All revisions used by tag symbol changesets have to have the
	# changeset's tag associated with them.
	CheckInCS \
	    {All revisions used by tag symbol changesets have to have the changeset's tag attached to them} \
	    {does not have the tag of its symbol changeset @ attached to it} {
		SELECT CT.name, C.cid, F.name, R.rev
		FROM   changeset C, cstype CT, revision R, file F, csrevision CR, tag T
		WHERE  C.type = 1       -- symbol changesets only
		AND    C.src  = T.sid   -- tag only, linked by symbol id 
		AND    C.cid  = CR.cid  -- changeset --> its revisions
		AND    R.rid  = CR.rid  -- look at the revisions
		-- and look for the tag among the attached ones.
		AND    T.sid NOT IN (SELECT TB.sid
				     FROM   tag TB
				     WHERE  TB.rev = R.rid)
		AND    R.fid = F.fid    -- get file of revision
	    }

	# All revisions used by branch symbol changesets have to have
	# the changeset's branch associated with them.

	CheckInCS \
	    {All revisions used by branch symbol changesets have to have the changeset's branch attached to them} \
	    {does not have the branch of its symbol changeset @ attached to it} {
		SELECT CT.name, C.cid, F.name, R.rev, C.cid
		FROM   changeset C, cstype CT, revision R, file F, csrevision CR, branch B
		WHERE  C.type = 1       -- symbol changesets only
		AND    C.src  = B.sid   -- branches only
		AND    C.cid  = CR.cid  -- changeset --> its revisions
		AND    R.rid  = CR.rid  -- look at the revisions
		-- and look for the branch among the attached ones.
		AND    B.sid NOT IN (SELECT BB.sid
				     FROM   branch BB
				     WHERE  BB.root = R.rid)
		AND    R.fid = F.fid    -- get file of revision
	    }

	# TODO
	# The state has to contain at least one tag symbol changeset
	# for all known tags.

	# TODO
	# The state has to contain at least one branch symbol changeset
	# for all known branches.
	return
    }


    proc Check {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {fname revnr} [state run $sql] {
	    set ok 0
	    trouble fatal "$fname <$revnr> $label"
	}
	log write 5 integrity "\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header"
	return
    }

    proc CheckCS {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {ctype cid} [state run $sql] {
	    set ok 0
	    trouble fatal "<$ctype $cid> $label"
	}
	log write 5 integrity "\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header"
	return
    }

    proc CheckInCS {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {cstype csid fname revnr} [state run $sql] {
	    set ok 0
	    set b "<$cstype $csid>"
	    trouble fatal "$fname <$revnr> [string map [list @ $b] $label]"
	}
	log write 5 integrity "\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header"
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export integrity
    namespace eval integrity {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register integrity
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::integrity 1.0
return
