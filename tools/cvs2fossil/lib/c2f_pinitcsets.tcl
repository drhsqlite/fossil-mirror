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

## Pass V. This pass creates the initial set of project level
## revisions, aka changesets. Later passes will refine them, puts them
## into proper order, set their dependencies, etc.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::misc                       ; # Text formatting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols
package require vc::fossil::import::cvs::project::rev ; # Project level changesets

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    InitCsets \
    {Initialize ChangeSets} \
    ::vc::fossil::import::cvs::pass::initcsets

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::initcsets {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define the names and structure of the persistent state of
	# this pass.

	state reading meta
	state reading revision
	state reading revisionbranchchildren
	state reading branch
	state reading tag
	state reading symbol

	# Data per changeset, namely the project it belongs to, how it
	# was induced (revision or symbol), plus reference to the
	# primary entry causing it (meta entry or symbol). An adjunct
	# table translates the type id's into human readable labels.

	state writing changeset {
	    cid   INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    pid   INTEGER  NOT NULL  REFERENCES project,
	    type  INTEGER  NOT NULL  REFERENCES cstype,
	    src   INTEGER  NOT NULL -- REFERENCES meta|symbol (type dependent)
	}
	state writing cstype {
	    tid   INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    name  TEXT     NOT NULL,
	    UNIQUE (name)
	}
	state run {
	    INSERT INTO cstype VALUES (0,'rev');
	    INSERT INTO cstype VALUES (1,'sym');
	}

	# Map from changesets to the (file level) revisions they
	# contain. The pos'ition provides an order of the revisions
	# within a changeset. They are unique within the changeset.
	# The revisions are in principle unique, if we were looking
	# only at revision changesets. However a revision can appear
	# in both revision and symbol changesets, and in multiple
	# symbol changesets as well. So we can only say that it is
	# unique within the changeset.
	#
	# TODO: Check if integrity checks are possible.

	state writing csrevision {
	    cid  INTEGER  NOT NULL  REFERENCES changeset,
	    pos  INTEGER  NOT NULL,
	    rid  INTEGER  NOT NULL  REFERENCES revision,
	    UNIQUE (cid, pos),
	    UNIQUE (cid, rid)
	}

	project::rev getcstypes
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	state reading changeset
	state reading csrevision
	state reading cstype

	foreach {id pid cstype srcid} [state run {
	    SELECT C.cid, C.pid, CS.name, C.src
	    FROM   changeset C, cstype CS
	    WHERE  C.type = CS.tid
	    ORDER BY C.cid
	}] {
	    set r [project::rev %AUTO% [repository projectof $pid] $cstype $srcid [state run {
		SELECT C.rid
		FROM   csrevision C
		WHERE  C.cid = $id
		ORDER  BY C.pos
	    }] $id]
	}

	project::rev getcstypes
	project::rev loadcounter
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	state transaction {
	    CreateRevisionChangesets  ; # Group file revisions into csets.
	    BreakInternalDependencies ; # Split the csets based on internal conflicts.
	    CreateSymbolChangesets    ; # Create csets for tags and branches.
	    PersistTheChangesets

	    repository printcsetstatistics
	}
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard changeset
	state discard cstype
	state discard csrevision
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc CreateRevisionChangesets {} {
	log write 3 initcsets {Create changesets based on revisions}

	# To get the initial of changesets we first group all file
	# level revisions using the same meta data entry together. As
	# the meta data encodes not only author and log message, but
	# also line of development and project we can be sure that
	# revisions in different project and lines of development are
	# not grouped together. In contrast to cvs2svn we do __not__
	# use distance in time between revisions to break them
	# apart. We have seen CVS repositories (from SF) where a
	# single commit contained revisions several hours apart,
	# likely due to trouble on the server hosting the repository.

	# We order the revisions here by time, this will help the
	# later passes (avoids joins later to get at the ordering
	# info).

	set n 0

	set lastmeta    {}
	set lastproject {}
	set revisions   {}

	# Note: We could have written this loop to create the csets
	#       early, extending them with all their revisions. This
	#       however would mean lots of (slow) method invokations
	#       on the csets. Doing it like this, late creation, means
	#       less such calls. None, but the creation itself.

	foreach {mid rid pid} [state run {
	    SELECT M.mid, R.rid, M.pid
	    FROM   revision R, meta M   -- R ==> M, using PK index of M.
	    WHERE  R.mid = M.mid
	    ORDER  BY M.mid, R.date
	}] {
	    if {$lastmeta != $mid} {
		if {[llength $revisions]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    project::rev %AUTO% $p rev $lastmeta $revisions
		    set revisions {}
		}
		set lastmeta    $mid
		set lastproject $pid
	    }
	    lappend revisions $rid
	}

	if {[llength $revisions]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    project::rev %AUTO% $p rev $lastmeta $revisions
	}

	log write 4 initcsets "Created [nsp $n {revision changeset}]"
	return
    }

    proc CreateSymbolChangesets {} {
	log write 3 initcsets {Create changesets based on symbols}

	# Tags and branches induce changesets as well, containing the
	# revisions they are attached to (tags), or spawned from
	# (branches).

	set n 0

	# First process the tags, then the branches. We know that
	# their ids do not overlap with each other.

	set lastsymbol  {}
	set lastproject {}
	set revisions   {}

	foreach {sid rid pid} [state run {
	    SELECT S.sid, R.rid, S.pid
	    FROM  tag T, revision R, symbol S     -- T ==> R/S, using PK indices of R, S.
	    WHERE T.rev = R.rid
	    AND   T.sid = S.sid
	    ORDER BY S.sid, R.date
	}] {
	    if {$lastsymbol != $sid} {
		if {[llength $revisions]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    project::rev %AUTO% $p sym $lastsymbol $revisions
		    set revisions {}
		}
		set lastsymbol  $sid
		set lastproject $pid
	    }
	    lappend revisions $rid
	}

	if {[llength $revisions]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    project::rev %AUTO% $p sym $lastsymbol $revisions
	}

	set lastsymbol {}
	set lasproject {}
	set revisions  {}

	foreach {sid rid pid} [state run {
	    SELECT S.sid, R.rid, S.pid
	    FROM  branch B, revision R, symbol S  -- B ==> R/S, using PK indices of R, S.
	    WHERE B.root = R.rid
	    AND   B.sid  = S.sid
	    ORDER BY S.sid, R.date
	}] {
	    if {$lastsymbol != $sid} {
		if {[llength $revisions]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    project::rev %AUTO% $p sym $lastsymbol $revisions
		    set revisions {}
		}
		set lastsymbol  $sid
		set lastproject $pid
	    }
	    lappend revisions $rid
	}

	if {[llength $revisions]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    project::rev %AUTO% $p sym $lastsymbol $revisions
	}

	log write 4 initcsets "Created [nsp $n {symbol changeset}]"
	return
    }

    proc BreakInternalDependencies {} {
	# This code operates on the revision changesets created by
	# 'CreateRevisionChangesets'. As such it has to follow after
	# it, before the symbol changesets are made. The changesets
	# are inspected for internal conflicts and any such are broken
	# by splitting the problematic changeset into multiple
	# fragments. The results are changesets which have no internal
	# dependencies, only external ones.

	log write 3 initcsets {Break internal dependencies}
	set old [llength [project::rev all]]

	foreach cset [project::rev all] {
	    $cset breakinternaldependencies
	}

	set n [expr {[llength [project::rev all]] - $old}]
	log write 4 initcsets "Created [nsp $n {additional revision changeset}]"
	log write 4 initcsets Ok.
	return
    }

    proc PersistTheChangesets {} {
	log write 3 initcsets "Saving [nsp [llength [project::rev all]] {initial changeset}] to the persistent state"

	foreach cset [project::rev all] {
	    $cset persist
	}

	log write 4 initcsets Ok.
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
    namespace export initcsets
    namespace eval initcsets {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::log
	log register initcsets
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::initcsets 1.0
return
