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
	set n 0
	AllButMeta
	Meta
	return
    }

    typemethod metarelaxed {} {
	set n 0
	AllButMeta
	return
    }

    typemethod changesets {} {
	set n 0
	RevisionChangesets
	SymbolChangesets
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc AllButMeta {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent cross-references.
	log write 4 integrity {Check database consistency}

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
	log write 4 integrity {Check database consistency}

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
