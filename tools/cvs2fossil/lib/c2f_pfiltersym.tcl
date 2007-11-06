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

## Pass IV. Coming after the symbol collation pass this pass now
## removes all revisions and symbols referencing any of the excluded
## symbols from the persistent database.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
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

	state reading symbol
	state reading blocker
	state reading parent
	state reading preferedparent
	state reading revision
	state reading branch
	state reading tag
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
	    MutateTagsToBranch
	    MutateBranchesToTag

	    # Consider a rerun of the pass 2 paranoia checks.
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
	log write 3 filtersym "Filter out excluded symbols and users"

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

	foreach {id parent transfer} [state run {
	    SELECT R.rid, R.parent, R.dbchild
	    FROM  revision R, symbol S
	    WHERE R.lod = S.sid
	    AND   S.sid IN excludedsymbols
	    AND   R.isdefault
	}] {
	    set ntdb($id) $parent
	    if {$transfer eq ""} continue
	    set link($id) $transfer
	}

	foreach joint [array names link] {
	    # The joints are the highest NTDB revisions which are
	    # shared with their respective trunk. We disconnect from
	    # their NTDB children, and make them parents of their
	    # 'dbchild'. The associated 'dbparent' is squashed
	    # instead. All parents of the joints are moved to the
	    # trunk as well.

	    set tjoint $link($joint)
	    set tlod [lindex [state run {
		SELECT lod FROM revision WHERE rid = $tjoint
	    }] 0]

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
	    DELETE FROM revision WHERE lod IN excludedsymbols;
	    DELETE FROM tag      WHERE lod IN excludedsymbols;
	    DELETE FROM tag      WHERE sid IN excludedsymbols;
	    DELETE FROM branch   WHERE lod IN excludedsymbols;
	    DELETE FROM branch   WHERE sid IN excludedsymbols;

	    DROP TABLE excludedsymbols;
	}
	return
    }

    proc MutateTagsToBranch {} {
	log write 3 filtersym "Mutate tags to branches"

	# Next, now that we know which symbols are what we look for
	# file level tags which are actually converted as branches
	# (project level), and put them into the correct table.

	set branch [project::sym branch]

	set tagstomutate [state run {
	    SELECT T.tid, T.fid, T.lod, T.sid, T.rev
	    FROM tag T, symbol S
	    WHERE T.sid = S.sid
	    AND S.type = $branch
	}]
	foreach {id fid lod sid rev} $tagstomutate {
	    state run {
		DELETE FROM tag WHERE tid = $id ;
		INSERT INTO branch (bid, fid,  lod,  sid,  root, first, bra)
		VALUES             ($id, $fid, $lod, $sid, $rev, NULL,  '');
	    }
	}
	return
    }

    proc MutateBranchesToTag {} {
	log write 3 filtersym "Mutate branches to tags"

	# Next, now that we know which symbols are what we look for
	# file level branches which are actually converted as tags
	# (project level), and put them into the correct table.

	set tag [project::sym tag]

	set branchestomutate [state run {
	    SELECT B.bid, B.fid, B.lod, B.sid, B.root, B.first, B.bra
	    FROM branch B, symbol S
	    WHERE B.sid = S.sid
	    AND S.type = $tag
	}]
	foreach {id fid lod sid root first bra} $branchestomutate {
	    state run {
		DELETE FROM branch WHERE bid = $id ;
		INSERT INTO tag (tid, fid,  lod,  sid,  rev)
		VALUES          ($id, $fid, $lod, $sid, $root);
	    }
	}
	return
    }

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
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	namespace import ::vc::tools::log
	log register filtersym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::filtersym 1.0
return
