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

    typemethod assert {expression failmessage} {
	set ok [uplevel 1 [list ::expr $expression]]
	if {$ok} return
	trouble internal [uplevel 1 [list ::subst $failmessage]]
	return
    }

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
	RevisionChangesets
	TagChangesets
	BranchChangesets
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
	CheckRev \
	    {Revisions and their LODs have to be in the same project} \
	    {disagrees with its LOD about owning project} {
		SELECT F.name, R.rev
		FROM revision R, file F, symbol S
		WHERE R.fid = F.fid   -- get file of rev
		AND   R.lod = S.sid   -- get symbol of its lod
		AND   F.pid != S.pid  -- disagreement about the owning project
		;
	    }
	# Find all revisions which disgree with their meta data about
	# the project they are owned by.
	CheckRev \
	    {Revisions and their meta data have to be in the same project} \
	    {disagrees with its meta data about owning project} {
		SELECT F.name, R.rev
		FROM revision R, file F, meta M
		WHERE R.fid = F.fid   -- get file of rev
		AND   R.mid = M.mid   -- get meta of rev
		AND   F.pid != M.pid  -- disagreement about owning project
		;
	    }
	# Find all revisions with a primary child which disagrees
	# about the file they belong to.
	CheckRev \
	    {Revisions and their primary children have to be in the same file} \
	    {disagrees with its primary child about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid       -- get file of rev
		AND   R.child IS NOT NULL -- get all with primary children
		AND   R.child = C.rid     -- get primary child
		AND   C.fid != R.fid      -- wrongly in different file
		;
	    }

	# Find all revisions with a branch parent symbol whose parent
	# disagrees about the file they belong to.
	CheckRev \
	    {Revisions and their branch children have to be in the same file} \
	    {at the beginning of its branch and its parent disagree about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid         -- get file of rev
		AND   R.bparent IS NOT NULL -- get first-of-branch revisions
		AND   R.parent = P.rid      -- get out-of-branch parent
		AND   R.fid != P.fid        -- wrongly in different file
		;
	    }
	# Find all revisions with a non-NTDB child which disagrees
	# about the file they belong to.
	CheckRev \
	    {Revisions and their non-NTDB children have to be in the same file} \
	    {disagrees with its non-NTDB child about the owning file} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid         -- get file of rev
		AND   R.dbchild IS NOT NULL -- get last NTDB revisions
		AND   R.dbchild = C.rid     -- get their child
		AND   C.fid != R.fid        -- wrongly in different file
		;
	    }
	# Find all revisions which have a primary child, but the child
	# does not have them as parent.
	CheckRev \
	    {Revisions have to be parents of their primary children} \
	    {is not the parent of its primary child} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid         -- get file of rev
		AND   R.child IS NOT NULL   -- get all with primary children
		AND   R.child = C.rid       -- get primary child
		AND   C.parent != R.rid     -- child's parent wrongly not us
		;
	    }
	# Find all revisions which have a primrary child, but the
	# child has a branch parent symbol making them brach starters.
	CheckRev \
	    {Primary children of revisions must not start branches} \
	    {is parent of a primary child which is the beginning of a branch} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid         -- get file of rev
		AND   R.child IS NOT NULL   -- get all with primary children
		AND   R.child = C.rid       -- get primary child
		AND   C.bparent IS NOT NULL -- but indicates to be on branch
		;
	    }
	# Find all revisions without branch parent symbol which have a
	# parent, but the parent does not have them as primary child.
	CheckRev \
	    {Revisions have to be primary children of their parents, if any} \
	    {is not the child of its parent} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid        -- get file of revision
		AND   R.bparent IS NULL    -- exclude all first-on-branch revisions
		AND   R.parent IS NOT NULL -- which are not root of their line
		AND   R.parent = P.rid     -- get in-lod parent
		AND   P.child != R.rid     -- but does not have rev as primary child
		;
	    }
	# Find all revisions with a branch parent symbol which do not
	# have a parent.
	CheckRev \
	    {Branch starting revisions have to have a parent, if not detached} \
	    {at the beginning of its branch has no parent, but its branch has} {
		SELECT F.name, R.rev
		FROM revision R, file F, branch B
		WHERE R.fid = F.fid         -- get file of revision
		AND   R.bparent IS NOT NULL -- limit to first-on-branch revisions
		AND   R.parent  IS NULL     -- which are detached
		AND   B.sid = R.bparent     -- get branch governing the rev
		AND   B.fid = R.fid         -- in the revision's file
		AND   B.root    IS NOT NULL -- but says that branch is attached
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# has them as primary child.
	CheckRev \
	    {Branch starting revisions must not be primary children of their parents} \
	    {at the beginning of its branch is the primary child of its parent} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid         -- get file of revision
		AND   R.bparent IS NOT NULL -- limit to first-on-branch revisions
		AND   R.parent IS NOT NULL  -- which are attached
		AND   R.parent = P.rid      -- get out-of-branch parent
		AND   P.child = R.rid       -- wrongly has rev as primary child
		;
	    }
	# Find all revisions with a non-NTDB child which are not on
	# the NTDB.
	CheckRev \
	    {NTDB to trunk transition has to begin on NTDB} \
	    {has a non-NTDB child, yet is not on the NTDB} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid         -- get file of revision
		AND   R.dbchild IS NOT NULL -- limit to last NTDB revision
		AND   NOT R.isdefault       -- but signals not-NTDB
		;
	    }
	# Find all revisions with a NTDB parent which are on the NTDB.
	CheckRev \
	    {NTDB to trunk transition has to end on non-NTDB} \
	    {has a NTDB parent, yet is on the NTDB} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid          -- get file of revision
		AND   R.dbparent IS NOT NULL -- limit to roots of non-NTDB
		AND   R.isdefault            -- but signals to be NTDB
		;
	    }
	# Find all revisions with a child which disagrees about the
	# line of development they belong to.
	CheckRev \
	    {Revisions and their primary children have to be in the same LOD} \
	    {and its primary child disagree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid       -- get file of revision
		AND   R.child IS NOT NULL -- revision has a primary child
		AND   R.child = C.rid     -- get that child
		AND   C.lod != R.lod      -- child wrongly disagrees with lod
		;
	    }
	# Find all revisions with a non-NTDB child which agrees about
	# the line of development they belong to.
	CheckRev \
	    {NTDB and trunk revisions have to be in different LODs} \
	    {on NTDB and its non-NTDB child wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision C, file F
		WHERE R.fid = F.fid         -- get file of revision
		AND   R.dbchild IS NOT NULL -- limit to last NTDB revision
		AND   R.dbchild = C.rid     -- get non-NTDB child
		AND   C.lod = R.lod         -- child wrongly has same lod
		;
	    }
	# Find all revisions with a branch parent symbol which is not
	# their LOD.
	CheckRev \
	    {Branch starting revisions have to have their LOD as branch parent symbol} \
	    {at the beginning of its branch does not have the branch symbol as its LOD} {
		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.fid = F.fid         -- get file of revision
		AND   R.bparent IS NOT NULL -- limit to branch-first revisions
		AND   R.lod != R.bparent    -- out-of-branch parent wrongly is not the lod
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# is in the same line of development.
	CheckRev \
	    {Revisions and their branch children have to be in different LODs} \
	    {at the beginning of its branch and its parent wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM revision R, revision P, file F
		WHERE R.fid = F.fid          -- get file of revision
		AND   R.bparent IS NOT NULL  -- limit to branch-first revisions
		AND   R.parent = P.rid       -- get out-of-branch parent of revision
		AND   R.lod = P.lod          -- rev and parent wrongly agree on lod
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
	CheckRev \
	    {Revisions and their meta data have to be in the same LOD} \
	    {disagrees with its meta data about owning LOD} {
		SELECT F.name, R.rev
		FROM revision R, meta M, file F
		WHERE R.mid = M.mid   -- get meta data of revision
		AND   R.lod != M.bid  -- rev wrongly disagrees with meta about lod
		AND   R.fid = F.fid   -- get file of revision
		;
	    }
	return
    }

    proc RevisionChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all revisions which are not used by at least one
	# changeset.
	CheckRev \
	    {All revisions have to be used by least one changeset} \
	    {is not used by a revision changeset} {
		-- Unused revisions = All revisions
		--                  - revisions used by revision changesets.
		--
		-- Both sets can be computed easily, and subtracted
                -- from each other. Then we can get the associated
                -- file (name) for display.

		SELECT F.name, R.rev
		FROM revision R, file F
		WHERE R.rid IN (SELECT rid
				FROM revision                -- All revisions
				EXCEPT                       -- subtract
				SELECT CI.iid
				FROM csitem CI, changeset C  -- revisions used
				WHERE C.cid = CI.cid         -- by any revision
				AND C.type = 0)              -- changeset
		AND   R.fid = F.fid              -- get file of unused revision
	    }
	# Find all revisions which are used by more than one
	# changeset.
	CheckRev \
	    {All revisions have to be used by at most one changeset} \
	    {is used by multiple changesets} {
		-- Principle of operation: Get all revision/changeset
                -- pairs for all revision changesets, group by
                -- revision to aggregate the changeset, counting
                -- them. From the resulting revision/count table
                -- select those with more than one user, and get their
                -- associated file (name) for display.

		SELECT F.name, R.rev
		FROM revision R, file F,
		     (SELECT CI.iid        AS rid,  -- revision item
		             count(CI.cid) AS count -- number of csets using item
		      FROM csitem CI, changeset C
		      WHERE C.type = 0            -- limit to revision csets
		      AND   C.cid  = CI.cid       -- get item in changeset
		      GROUP BY CI.iid             -- aggregate by item, count csets/item
		     ) AS U
		WHERE U.count > 1    -- limit to item with multiple users
		AND   R.rid = U.rid  -- get revision of item
		AND   R.fid = F.fid  -- get file of revision
	    }
	# All revisions have to refer to the same meta information as
	# their changeset.
	CheckRevCS \
	    {All revisions have to agree with their changeset about the used meta information} \
	    {disagrees with its changeset @ about the meta information} {
		SELECT CT.name, C.cid, F.name, R.rev
		FROM changeset C, cstype CT, revision R, file F, csitem CI
		WHERE C.type = 0       -- revision changesets only
		AND   C.cid  = CI.cid  -- changeset --> its revisions
		AND   R.rid  = CI.iid  -- look at them
		AND   R.mid != C.src   -- Only those which disagree with changeset about the meta
		AND   R.fid = F.fid    -- get file of the revision
		AND   CT.tid = C.type  -- get changeset type, for labeling
	    }
	# All revisions have to agree on the LOD their changeset
	# belongs to. In other words, all revisions in a changeset
	# have to refer to the same line of development.
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
				 FROM (SELECT DISTINCT       -- unique cset/lod pairs
				              CI.cid AS cid, -- revision cset
				              R.lod  AS lod  -- lod of item in cset
				       FROM   csitem CI, changeset C, revision R
				       WHERE  CI.iid = R.rid  -- get rev of item in cset
				       AND    C.cid  = CI.cid -- get changeset of item
				       AND    C.type = 0      -- limit to rev csets
				      ) AS U
				 GROUP BY U.cid          -- aggregate by cset, count lods/cset
				 HAVING COUNT(U.lod) > 1 -- find csets with multiple lods
				)
		AND    T.tid = C.type
	    }
	# All revisions have to agree on the project their changeset
	# belongs to. In other words, all revisions in a changeset
	# have to refer to the same project.
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
				 FROM (SELECT DISTINCT       -- unique cset/proj pairs
				              CI.cid AS cid, -- rev cset
				              F.pid  AS pid  -- project of item in cset
				       FROM   csitem CI, changeset C, revision R, file F
				       WHERE  CI.iid = R.rid  -- get rev of item in cset
				       AND    C.cid  = CI.cid -- get changeset of item
				       AND    C.type = 0      -- limit to rev changesets
				       AND    F.fid  = R.fid  -- get file of revision
				      ) AS U
				 GROUP BY U.cid          -- aggregate by csets, count proj/cset
				 HAVING COUNT(U.pid) > 1 -- find csets with multiple projects
				)
		AND    T.tid = C.type -- get readable changeset type
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
				 FROM (SELECT U.cid         AS cid,   -- rev changeset
				              COUNT (U.fid) AS fcount -- number of files by items
				       FROM (SELECT DISTINCT       -- unique cset/file pairs
					            CI.cid AS cid, -- rev changeset
					            R.fid AS fid   -- file of item in changeset
					     FROM   csitem CI, changeset C, revision R
					     WHERE  CI.iid = R.rid  -- get rev of item in changeset
					     AND    C.cid  = CI.cid -- get changeset of item
					     AND    C.type = 0      -- limit to rev csets
					     ) AS U
				       GROUP BY U.cid -- aggregate by csets, count files/cset
				      ) AS UU,
				      (SELECT V.cid         AS cid,   -- rev changeset
				              COUNT (V.iid) AS rcount -- number of items
				       FROM   csitem V, changeset X
				       WHERE  X.cid  = V.cid  -- get changeset of item
				       AND    X.type = 0      -- limit to rev csets
				       GROUP BY V.cid         -- aggregate by csets, count items/cset
				      ) AS VV
				 WHERE VV.cid = UU.cid        -- sync #items/cset with #files/cset
				 AND   UU.fcount < VV.rcount  -- less files than items
				                              -- => items belong to the same file.
				)
		AND    T.tid = C.type -- get readable changeset type
	    }
	return
    }

    proc TagChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all tags which are not used by at least one changeset.
	CheckTag \
	    {All tags have to be used by least one changeset} \
	    {is not used by a tag symbol changeset} {
		-- Unused tags = All tags
		--             - revisions used by tag changesets.
		--
		-- Both sets can be computed easily, and subtracted
                -- from each other. Then we can get the associated
                -- file (name) for display.

		SELECT P.name, S.name
		FROM project P, tag T, symbol S
		WHERE T.tid IN (SELECT tid                    -- All tags
				FROM   tag
				EXCEPT                        -- subtract
				SELECT CI.iid                 -- tags used
				FROM   csitem CI, changeset C
				WHERE  C.cid = CI.cid         -- by any tag
				AND    C.type = 1)            -- changeset
		AND   S.sid = T.sid               -- get symbol of tag
		AND   P.pid = S.pid               -- get project of symbol
	    }
	# Find all tags which are used by more than one changeset.
	CheckRev \
	    {All tags have to be used by at most one changeset} \
	    {is used by multiple changesets} {
		-- Principle of operation: Get all tag/changeset pairs
                -- for all tag changesets, group by tag to aggregate
                -- the changeset, counting them. From the resulting
                -- tag/count table select those with more than one
                -- user, and get their associated file (name) for
                -- display.

		SELECT P.name, S.name
		FROM tag T, project P, symbol S,
		     (SELECT CI.iid        AS iid,  -- item
		             count(CI.cid) AS count -- number of csets using item
		      FROM csitem CI, changeset C
		      WHERE C.type = 1       -- limit to tag csets
		      AND   C.cid  = CI.cid  -- get items of cset
		      GROUP BY CI.iid        -- aggregate by item, count csets/item
		     ) AS U
		WHERE U.count > 1            -- find tag item used multiple times
		AND   T.tid = U.iid          -- get tag of item
		AND   S.sid = T.sid          -- get symbol of tag
		AND   P.pid = S.pid          -- get project of symbol
	    }
	if 0 {
	    # This check is disabled for the moment. Apparently tags
	    # can cross lines of development, at least if the involved
	    # LODs are the trunk, and the NTDB. That makes sense, as
	    # the NTDB revisions are initially logically a part of the
	    # trunk. The standard check below however does not capture
	    # this. When I manage to rephrase it to accept this type
	    # of cross-over it will be re-activated.

	    # All tags have to agree on the LOD their changeset
	    # belongs to. In other words, all tags in a changeset have
	    # to refer to the same line of development.
	    #
	    # Instead of looking at all pairs of tags in all
	    # changesets we generate the distinct set of all LODs
	    # referenced by the tags of a changeset, look for those
	    # with cardinality > 1, and get the identifying
	    # information for the changesets found thusly.
	    CheckCS \
		{All tags in a changeset have to belong to the same LOD} \
		{: Its tags disagree about the LOD they belong to} {
		    SELECT T.name, C.cid
		    FROM   changeset C, cstype T
		    WHERE  C.cid IN (SELECT U.cid
				     FROM (SELECT DISTINCT CI.cid AS cid, T.lod AS lod
					   FROM   csitem CI, changeset C, tag T
					   WHERE  CI.iid = T.tid
					   AND    C.cid = CI.cid
					   AND    C.type = 1) AS U
				     GROUP BY U.cid HAVING COUNT(U.lod) > 1)
		    AND    T.tid = C.type
		}
	}
	# All tags have to agree on the project their changeset
	# belongs to. In other words, all tags in a changeset have to
	# refer to the same project.
	#
	# Instead of looking at all pairs of tags in all changesets we
	# generate the distinct set of all projects referenced by the
	# tags of a changeset, look for those with cardinality > 1,
	# and get the identifying information for the changesets found
	# thusly.
	CheckCS \
	    {All tags in a changeset have to belong to the same project} \
	    {: Its tags disagree about the project they belong to} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT U.cid
				 FROM (SELECT DISTINCT       -- unique cset/proj pairs
				              CI.cid AS cid, -- tag cset
				              F.pid  AS pid  -- project of item in cset
				       FROM   csitem CI, changeset C, tag T, file F
				       WHERE  CI.iid = T.tid  -- get tag of item in cset
				       AND    C.cid  = CI.cid -- get changeset of item
				       AND    C.type = 1      -- limit to tag changesets
				       AND    F.fid  = T.fid  -- get file of tag
                                      ) AS U
				 GROUP BY U.cid           -- aggregate by csets, count proj/cset
				 HAVING COUNT(U.pid) > 1  -- find csets with multiple projects
		                )
		AND    T.tid = C.type -- get readable changeset type
	    }
	# All tags in a single changeset have to belong to different
	# files. Conversely: No two tags of a single file are allowed
	# to be in the same changeset.
	#
	# Instead of looking at all pairs of tags in all changesets we
	# generate the distinct set of all files referenced by the
	# tags of a changeset, and look for those with cardinality <
	# the cardinality of the set of tags, and get the identifying
	# information for the changesets found thusly.
	CheckCS \
	    {All tags in a changeset have to belong to different files} \
	    {: Its tags share files} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT VV.cid
				 FROM (SELECT U.cid         AS cid,   -- changeset
				              COUNT (U.fid) AS fcount -- number of files by items
				       FROM (SELECT DISTINCT       -- unique cset/file pairs
					            CI.cid AS cid, -- tag changeset
					            T.fid  AS fid  -- file of item in changeset
					     FROM   csitem CI, changeset C, tag T
					     WHERE  CI.iid = T.tid -- get tag of item in changeset
					     AND    C.cid = CI.cid -- get changeset of item
					     AND    C.type = 1     -- limit to tag changesets
					     ) AS U
				       GROUP BY U.cid -- aggregate by csets, count files/cset
                                      ) AS UU,
				      (SELECT V.cid         AS cid,   -- changeset
				              COUNT (V.iid) AS rcount -- number of items in cset
				       FROM   csitem V, changeset X
				       WHERE  X.cid  = V.cid -- get changeset of item
				       AND    X.type = 1     -- limit to tag changesets
				       GROUP BY V.cid        -- aggregate by csets, count items/cset
                                      ) AS VV
				 WHERE VV.cid = UU.cid       -- sync #items/cset with #files/cset
				 AND   UU.fcount < VV.rcount -- less files than items
				                             -- => items belong to the same file.
				)
		AND    T.tid = C.type -- get readable changeset type
	    }
	return
    }

    proc BranchChangesets {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent changeset/revision
	# information.

	upvar 1 n n ; # Counter for the checks (we print an id before
		      # the main label).

	# Find all branches which are not used by at least one
	# changeset.
	CheckBranch \
	    {All branches have to be used by least one changeset} \
	    {is not used by a branch symbol changeset} {
		-- Unused branches = All branches
		--                 - branches used by branch changesets.
		--
		-- Both sets can be computed easily, and subtracted
                -- from each other. Then we can get the associated
                -- file (name) for display.

		SELECT P.name, S.name
		FROM project P, branch B, symbol S
		WHERE B.bid IN (SELECT bid                    -- All branches
				FROM   branch
				EXCEPT                        -- subtract
				SELECT CI.iid                 -- branches used
				FROM   csitem CI, changeset C
				WHERE  C.cid = CI.cid         -- by any branch
				AND    C.type = 2             -- changeset
			       )
		AND   S.sid = B.sid               -- get symbol of branch
		AND   P.pid = S.pid               -- get project of symbol
	    }
	# Find all branches which are used by more than one changeset.
	CheckRev \
	    {All branches have to be used by at most one changeset} \
	    {is used by multiple changesets} {
		-- Principle of operation: Get all branch/changeset
                -- pairs for all branch changesets, group by tag to
                -- aggregate the changeset, counting them. From the
                -- resulting branch/count table select those with more
                -- than one user, and get their associated file (name)
                -- for display.

		SELECT P.name, S.name
		FROM branch B, project P, symbol S,
		     (SELECT CI.iid        AS iid,  -- item
                             count(CI.cid) AS count -- number of csets for item
		      FROM csitem CI, changeset C
		      WHERE C.type = 2        -- limit to branch changesets,
		      AND   C.cid = CI.cid    -- get the items they contain,
		      GROUP BY CI.iid         -- aggregate by items, count csets/item (x)
                     ) AS U
		WHERE U.count > 1             -- find items used multiple times
		AND   B.bid = U.iid           -- get the users (branch changesets)
		AND   S.sid = B.sid           -- get symbol of branch
		AND   P.pid = S.pid           -- get project of symbol
	    }
	if 0 {
	    # This check has been disabled. When the converter was run
	    # on the Tcl CVS several branches tripped this
	    # constraint. One of them was a free-floating branch, and
	    # its handling has been fixed by now. The others however
	    # seem semi-legitimate, in the sense that they show
	    # inconsistencies in the CVS history the user is not
	    # really able to solve, but it might be possible to simply
	    # ignore them.

	    # For example in Tcl we have a branch X with a prefered
	    # parent Y, except for a single file where the prefered
	    # parent seems to be created after its current parent,
	    # making re-parenting impossible. However we may be able
	    # to ignore this, it should only cause the branch to have
	    # more than one predecessor, and shifting it around in the
	    # commit order. The backend would still use the prefered
	    # parent for the attachment point in fossil.

	    # So, for now I have decided to disable this and press
	    # forward. Of course, if we run into actual trouble we
	    # will have to go back here see what can be done to fix
	    # this. Even if only giving the user the instruction how
	    # to edit the CVS repository to remove the inconsistency.

	    # All branches have to agree on the LOD their changeset
	    # belongs to. In other words, all branches in a changeset
	    # have to refer to the same line of development.
	    #
	    # Instead of looking at all pairs of branches in all
	    # changesets we generate the distinct set of all LODs
	    # referenced by the branches of a changeset, look for
	    # those with cardinality > 1, and get the identifying
	    # information for the changesets found thusly.
	    CheckCS \
		{All branches in a changeset have to belong to the same LOD} \
		{: Its branches disagree about the LOD they belong to} {
		    SELECT T.name, C.cid
		    FROM   changeset C, cstype T
		    WHERE  C.cid IN (SELECT U.cid
				     FROM (SELECT DISTINCT CI.cid AS cid, B.lod AS lod
					   FROM   csitem CI, changeset C, branch B
					   WHERE  CI.iid = B.bid
					   AND    C.cid = CI.cid
					   AND    C.type = 2) AS U
				     GROUP BY U.cid HAVING COUNT(U.lod) > 1)
		    AND    T.tid = C.type
		}
	}
	# All branches have to agree on the project their changeset
	# belongs to. In other words, all branches in a changeset have
	# to refer to the same project.
	#
	# Instead of looking at all pairs of branches in all
	# changesets we generate the distinct set of all projects
	# referenced by the branches of a changeset, look for those
	# with cardinality > 1, and get the identifying information
	# for the changesets found thusly.
	CheckCS \
	    {All branches in a changeset have to belong to the same project} \
	    {: Its branches disagree about the project they belong to} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT U.cid
				 FROM (SELECT DISTINCT        -- Unique cset/proj pairs
				              CI.cid AS cid,  -- Branch cset
				              F.pid  AS pid   -- Project of item in cset
				       FROM   csitem CI, changeset C, branch B, file F
				       WHERE  CI.iid = B.bid  -- get branch of item in cset
				       AND    C.cid  = CI.cid -- get changeset of item
				       AND    C.type = 2      -- limit to branch changesets
				       AND    F.fid  = B.fid  -- get file of branch
                                      ) AS U
				 GROUP BY U.cid          -- aggregate by csets, count proj/cset
				 HAVING COUNT(U.pid) > 1 -- find cset with multiple projects
				)
		AND    T.tid = C.type -- get readable changeset type
	    }
	# All branches in a single changeset have to belong to
	# different files. Conversely: No two branches of a single
	# file are allowed to be in the same changeset.
	#
	# Instead of looking at all pairs of branches in all
	# changesets we generate the distinct set of all files
	# referenced by the branches of a changeset, and look for
	# those with cardinality < the cardinality of the set of
	# branches, and get the identifying information for the
	# changesets found thusly.
	CheckCS \
	    {All branches in a changeset have to belong to different files} \
	    {: Its branches share files} {
		SELECT T.name, C.cid
		FROM   changeset C, cstype T
		WHERE  C.cid IN (SELECT VV.cid
				 FROM (SELECT U.cid         AS cid,   -- changeset
				              COUNT (U.fid) AS fcount -- number of files by items
				       FROM (SELECT DISTINCT       -- unique cset/file pairs
					            CI.cid AS cid, -- Branch changeset
					            B.fid  AS fid  -- File of item in changeset
					     FROM   csitem CI, changeset C, branch B
					     WHERE  CI.iid = B.bid  -- get tag of item in changeset
					     AND    C.cid  = CI.cid -- get changeset of item
					     AND    C.type = 2      -- limit to branch changesets
					     ) AS U
				       GROUP BY U.cid -- aggregate by csets, count files/cset
				      ) AS UU,
				      (SELECT V.cid         AS cid,   -- changeset
				              COUNT (V.iid) AS rcount -- number of items in cset
				       FROM   csitem V, changeset X
				       WHERE  X.cid  = V.cid -- get changeset of item
				       AND    X.type = 2     -- limit to branch changesets
				       GROUP BY V.cid	     -- aggregate by csets, count items/cset
				      ) AS VV
				 WHERE VV.cid = UU.cid       -- sync #items/cset with #files/cset
				 AND   UU.fcount < VV.rcount -- less files than items
							     -- => items belong to the same file.
				)
		AND    T.tid = C.type -- get readable changeset type
	    }
	return
    }

    proc ___UnusedChangesetChecks___ {} {
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
	CheckRevCS \
	    {All revisions used by tag symbol changesets have to have the changeset's tag attached to them} \
	    {does not have the tag of its symbol changeset @ attached to it} {
		SELECT CT.name, C.cid, F.name, R.rev
		FROM   changeset C, cstype CT, revision R, file F, csitem CI, tag T
		WHERE  C.type = 1       -- symbol changesets only
		AND    C.src  = T.sid   -- tag only, linked by symbol id
		AND    C.cid  = CI.cid  -- changeset --> its revisions
		AND    R.rid  = CI.iid  -- look at the revisions
		-- and look for the tag among the attached ones.
		AND    T.sid NOT IN (SELECT TB.sid
				     FROM   tag TB
				     WHERE  TB.rev = R.rid)
		AND    R.fid = F.fid    -- get file of revision
	    }

	# All revisions used by branch symbol changesets have to have
	# the changeset's branch associated with them.

	CheckRevCS \
	    {All revisions used by branch symbol changesets have to have the changeset's branch attached to them} \
	    {does not have the branch of its symbol changeset @ attached to it} {
		SELECT CT.name, C.cid, F.name, R.rev, C.cid
		FROM   changeset C, cstype CT, revision R, file F, csitem CI, branch B
		WHERE  C.type = 1       -- symbol changesets only
		AND    C.src  = B.sid   -- branches only
		AND    C.cid  = CI.cid  -- changeset --> its revisions
		AND    R.rid  = CI.iid  -- look at the revisions
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


    proc CheckRev {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {fname revnr} [state run $sql] {
	    set ok 0
	    trouble fatal "${revnr}::$fname $label"
	}
	log write 5 integrity {\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header}
	return
    }

    proc CheckTag {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {pname sname} [state run $sql] {
	    set ok 0
	    trouble fatal "<$pname tag '$sname'> $label"
	}
	log write 5 integrity {\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header}
	return
    }

    proc CheckBranch {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {pname sname} [state run $sql] {
	    set ok 0
	    trouble fatal "<$pname branch '$sname'> $label"
	}
	log write 5 integrity {\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header}
	return
    }

    proc CheckCS {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {ctype cid} [state run $sql] {
	    set ok 0
	    trouble fatal "<$ctype $cid> $label"
	}
	log write 5 integrity {\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header}
	return
    }

    proc CheckRevCS {header label sql} {
	upvar 1 n n
	set ok 1
	foreach {cstype csid fname revnr} [state run $sql] {
	    set ok 0
	    set b "<$cstype $csid>"
	    trouble fatal "$fname <$revnr> [string map [list @ $b] $label]"
	}
	log write 5 integrity {\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header}
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
