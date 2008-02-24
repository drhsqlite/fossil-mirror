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

## Pass V. This pass creates the initial set of project level
## revisions, aka changesets. Later passes will refine them, puts them
## into proper order, set their dependencies, etc.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::misc                       ; # Text formatting.
package require vc::tools::log                        ; # User feedback.
package require vc::tools::mem                        ; # Memory tracking.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.
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

	state use project
	state use file
	state use revision
	state use revisionbranchchildren
	state use branch
	state use tag
	state use symbol
	state use meta

	# Data per changeset, namely the project it belongs to, how it
	# was induced (revision or symbol), plus reference to the
	# primary entry causing it (meta entry or symbol). An adjunct
	# table translates the type id's into human readable labels.

	state extend changeset {
	    cid   INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    pid   INTEGER  NOT NULL  REFERENCES project,
	    type  INTEGER  NOT NULL  REFERENCES cstype,
	    src   INTEGER  NOT NULL -- REFERENCES meta|symbol (type dependent)
	}
	state extend cstype {
	    tid   INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    name  TEXT     NOT NULL,
	    UNIQUE (name)
	}
	# Note: Keep the labels used here in sync with the names for
	#       singleton helper classes for 'project::rev'. They are
	#       the valid type names for changesets and also hardwired
	#       in some code.
	state run {
	    INSERT INTO cstype VALUES (0,'rev');
	    INSERT INTO cstype VALUES (1,'sym::tag');
	    INSERT INTO cstype VALUES (2,'sym::branch');
	}

	# Map from changesets to the (file level) revisions, tags, or
	# branches they contain. The pos'ition provides an order of
	# the items within a changeset. They are unique within the
	# changeset.  The items are in principle unique, if we were
	# looking only at relevant changesets. However as they come
	# from disparate sources the same id may have different
	# meaning, be in different changesets and so is formally not
	# unique. So we can only say that it is unique within the
	# changeset. The integrity module has stronger checks.

	state extend csitem {
	    cid  INTEGER  NOT NULL  REFERENCES changeset,
	    pos  INTEGER  NOT NULL,
	    iid  INTEGER  NOT NULL, -- REFERENCES revision|tag|branch
	    UNIQUE (cid, pos),
	    UNIQUE (cid, iid)
	} { iid }
	# Index on: iid (successor/predecessor retrieval)

	project::rev getcstypes
	return
    }

    typemethod load {} {
	# Pass manager interface. Executed to load data computed by
	# this pass into memory when this pass is skipped instead of
	# executed.

	state use changeset
	state use csitem
	state use cstype

	# Need the types first, the constructor used inside of the
	# 'load' below uses them to assert the correctness of type
	# names.
	project::rev getcstypes
	project::rev load ::vc::fossil::import::cvs::repository
	project::rev loadcounter
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	state transaction {
	    CreateRevisionChangesets  ; # Group file revisions into
					# preliminary csets and split
					# them based on internal
					# conflicts.
	    CreateSymbolChangesets    ; # Create csets for tags and
					# branches.
	}

	repository printcsetstatistics
	integrity changesets

	# Load the changesets for use by the next passes.
	project::rev load ::vc::fossil::import::cvs::repository
	project::rev loadcounter
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard changeset
	state discard cstype
	state discard csitem
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

	# The changesets made from these groups are immediately
	# inspected for internal conflicts and any such are broken by
	# splitting the problematic changeset into multiple
	# fragments. The results are changesets which have no internal
	# dependencies, only external ones.

	set n  0
	set nx 0

	set lastmeta    {}
	set lastproject {}
	set revisions   {}

	# Note: We could have written this loop to create the csets
	#       early, extending them with all their revisions. This
	#       however would mean lots of (slow) method invokations
	#       on the csets. Doing it like this, late creation, means
	#       less such calls. None, but the creation itself.

	log write 14 initcsets meta_begin
	mem::mark
	state foreachrow {
	    SELECT M.mid AS xmid,
	           R.rid AS xrid,
	           M.pid AS xpid
	    FROM   revision R,
	           meta     M   -- R ==> M, using PK index of M.
	    WHERE  R.mid = M.mid
	    ORDER  BY M.mid, R.date
	} {
	    log write 14 initcsets meta_next

	    if {$lastmeta != $xmid} {
		if {[llength $revisions]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    log write 14 initcsets meta_cset_begin
		    mem::mark
		    set cset [project::rev %AUTO% $p rev $lastmeta $revisions]
		    log write 14 initcsets meta_cset_done
		    set spawned [$cset breakinternaldependencies nx]
		    $cset persist
		    $cset destroy
		    foreach cset $spawned { $cset persist ; $cset destroy }
		    mem::mark
		    set revisions {}
		}
		set lastmeta    $xmid
		set lastproject $xpid
	    }
	    lappend revisions $xrid
	}

	if {[llength $revisions]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    log write 14 initcsets meta_cset_begin
	    mem::mark
	    set cset [project::rev %AUTO% $p rev $lastmeta $revisions]
	    log write 14 initcsets meta_cset_done
	    set spawned [$cset breakinternaldependencies nx]
	    $cset persist
	    $cset destroy
	    foreach cset $spawned { $cset persist ; $cset destroy }
	    mem::mark
	}

	log write 14 initcsets meta_done
	mem::mark

	log write 4 initcsets "Created and saved [nsp $n {revision changeset}]"
	log write 4 initcsets "Created and saved [nsp $nx {additional revision changeset}]"

	mem::mark
	log write 4 initcsets Ok.
	return
    }

    proc CreateSymbolChangesets {} {
	log write 3 initcsets {Create changesets based on symbols}
	mem::mark

	# Tags and branches induce changesets as well, containing the
	# revisions they are attached to (tags), or spawned from
	# (branches).

	set n 0

	# First process the tags, then the branches. We know that
	# their ids do not overlap with each other.

	set lastsymbol  {}
	set lastproject {}
	set tags        {}

	state foreachrow {
	    SELECT S.sid AS xsid,
	           T.tid AS xtid,
	           S.pid AS xpid
	    FROM  tag    T,
	          symbol S     -- T ==> R/S, using PK indices of R, S.
	    WHERE T.sid = S.sid
	    ORDER BY S.sid, T.tid
	} {
	    if {$lastsymbol != $xsid} {
		if {[llength $tags]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    set cset [project::rev %AUTO% $p sym::tag $lastsymbol $tags]
		    set tags {}
		    $cset persist
		    $cset destroy
		}
		set lastsymbol  $xsid
		set lastproject $xpid
	    }
	    lappend tags $xtid
	}

	if {[llength $tags]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    set cset [project::rev %AUTO% $p sym::tag $lastsymbol $tags]
	    $cset persist
	    $cset destroy
	}

	set lastsymbol {}
	set lasproject {}
	set branches   {}

	state foreachrow {
	    SELECT S.sid AS xsid,
	           B.bid AS xbid,
	           S.pid AS xpid
	    FROM  branch B,
	          symbol S  -- B ==> R/S, using PK indices of R, S.
	    WHERE B.sid  = S.sid
	    ORDER BY S.sid, B.bid
	} {
	    if {$lastsymbol != $xsid} {
		if {[llength $branches]} {
		    incr n
		    set  p [repository projectof $lastproject]
		    set cset [project::rev %AUTO% $p sym::branch $lastsymbol $branches]
		    set branches {}
		    $cset persist
		    $cset destroy
		}
		set lastsymbol  $xsid
		set lastproject $xpid
	    }
	    lappend branches $xbid
	}

	if {[llength $branches]} {
	    incr n
	    set  p [repository projectof $lastproject]
	    set cset [project::rev %AUTO% $p sym::branch $lastsymbol $branches]
	    $cset persist
	    $cset destroy
	}

	log write 4 initcsets "Created and saved [nsp $n {symbol changeset}]"
	mem::mark
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
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace eval mem {
	    namespace import ::vc::tools::mem::mark
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
