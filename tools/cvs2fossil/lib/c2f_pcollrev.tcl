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

## Pass II. This pass parses the collected rcs archives and extracts
## all the information they contain (revisions, and symbols).

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::pass         ; # Pass management.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols.
package require vc::fossil::import::cvs::file::rev    ; # File level revisions.
package require vc::rcs::parser                       ; # Rcs archive data extraction.

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    CollectRev \
    {Collect revisions and symbols} \
    ::vc::fossil::import::cvs::pass::collrev

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::collrev {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define names and structure of the persistent state of this
	# pass.

	state use project
	state use file

	# We deal with per project and per file data, the first
	# collated from the second.

	# Per file we have general information, ..., and then
	# revisions and symbols. The latter can be further separated
	# into tags and branches. At project level the per-file
	# symbols information is merged.

	# File level ...
	#	Revisions, Branches, Tags
	#
	# Pseudo class hierarchy
	#	Tag      <- Symbol <- Event
	#	Branch   <- Symbol <- Event
	#	Revision           <- Event

	state extend revision {
	    -- Revisions. Identified by a global numeric id each
	    -- belongs to a single file, identified by its id. It
	    -- further has a dotted revision number (DTN).
	    --
	    -- Constraint: The dotted revision number is unique within
            --             the file. See end of definition.

	    rid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    fid  INTEGER  NOT NULL  REFERENCES file,   -- File owning revision.
	    rev  TEXT     NOT NULL,                    -- Dotted Rev Number.

	    -- All revisions belong to a line-of-development,
	    -- identified by a symbol (project level). During data
	    -- collection it was a file-level branch symbol.
	    --
	    -- Constraint: All the LOD symbols are in the same project
	    --             as the file itself. This cannot be
	    --             expressed in CREATE TABLE syntax.

	    lod  INTEGER  NOT NULL  REFERENCES symbol, -- Line of development

	    -- The revisions in a file are organized in a forest of
	    -- trees, with the main lines defined through the parent /
	    -- child references. A revision without a parent is the
	    -- root of a tree, and a revision without a child is a
	    -- leaf.

	    -- Constraints: All revisions coupled through parent/child
	    --              refer to the same LOD symbol. The parent
	    --              of a child of X is X. The child of a
	    --              parent of X is X.

	    parent  INTEGER            REFERENCES revision,
	    child   INTEGER            REFERENCES revision,

	    -- The representation of a branch in a tree is the
	    -- exception to the three constraints above.

	    -- The beginning of a branch is represented by a non-NULL
	    -- bparent of a revision. This revision B is the first on
	    -- the branch. Its parent P is the revision the branch is
	    -- rooted in, and it is not the child of P. B and P refer
	    -- to different LOD symbols. The bparent of B is also its
	    -- LOD, and the LOD of its children.

	    bparent INTEGER            REFERENCES symbol,

	    -- Lastly we keep information is about non-trunk default
	    -- branches (NTDB) in the revisions.

	    -- All revisions on the NTDB have 'isdefault' TRUE,
	    -- everyone else FALSE. The last revision X on the NTDB
	    -- which is still considered to be on the trunk as well
	    -- has a non-NULL 'dbchild' which refers to the root of
	    -- the trunk. The root also has a non-NULL dbparent
	    -- refering to X.

	    isdefault INTEGER  NOT NULL,
	    dbparent  INTEGER            REFERENCES revision,
	    dbchild   INTEGER            REFERENCES revision,

	    -- The main payload of the revision are the date/time it
	    -- was entered, its state, operation (= type/class), text
	    -- content, and meta data (author, log message, branch,
	    -- project). The last is encoded as single id, see table
	    -- 'meta'. The date/time is given in seconds since the
	    -- epoch, for easy comparison.

	    op    INTEGER  NOT NULL  REFERENCES optype,
	    date  INTEGER  NOT NULL,
	    state TEXT     NOT NULL,
	    mid   INTEGER  NOT NULL  REFERENCES meta,

	    UNIQUE (fid, rev) -- The DTN is unique within the revision's file.
	}

	# Blobs contain the information needed to extract revisions
	# from rcs archive files. As such each revision has an
	# associated blob. However we can have blobs without
	# revisions. This happens if a logically irrelevant revision
	# is removed. We may however still need its blob to correctly
	# expand other revisions, both its contents and for the
	# ordering.

	state extend blob {
	    bid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    rid  INTEGER            REFERENCES revision,
	    fid  INTEGER  NOT NULL  REFERENCES file,   -- File owning blob.

	    -- The text content is an (offset,length) pair into the
	    -- rcs archive. For deltas we additionally refer to the
	    -- parent blob the delta is made against.

	    coff INTEGER  NOT NULL,
	    clen INTEGER  NOT NULL,
	    pid  INTEGER            REFERENCES blob,

	    UNIQUE (rid)
	} { fid }
	# Index on owning file to collect all blobs of a file when the
	# time for its expansion comes.

	state extend optype {
	    oid   INTEGER  NOT NULL  PRIMARY KEY,
	    name  TEXT     NOT NULL,
	    UNIQUE(name)
	}
	state run {
	    INSERT INTO optype VALUES (-1,'delete');  -- The opcode names are the
	    INSERT INTO optype VALUES ( 0,'nothing'); -- fixed pieces, see myopstate
	    INSERT INTO optype VALUES ( 1,'add');     -- in file::rev. myopcode is
	    INSERT INTO optype VALUES ( 2,'change');  -- loaded from this.
	}

	state extend revisionbranchchildren {
	    -- The non-primary children of a revision, as reachable
	    -- through a branch symbol, are listed here. This is
	    -- needed by pass 5 to break internal dependencies in a
	    -- changeset.

	    rid   INTEGER  NOT NULL  REFERENCES revision,
	    brid  INTEGER  NOT NULL  REFERENCES revision,
	    UNIQUE(rid,brid)
	}

	state extend tag {
	    tid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    fid  INTEGER  NOT NULL  REFERENCES file,     -- File the item belongs to
	    lod  INTEGER            REFERENCES symbol,   -- Line of development (NULL => Trunk)
	    sid  INTEGER  NOT NULL  REFERENCES symbol,   -- Symbol capturing the tag

	    rev  INTEGER  NOT NULL  REFERENCES revision  -- The revision being tagged.
	} { rev sid }
	# Indices on: rev (revision successors)
	#             sid (tag predecessors, branch successors/predecessors)

	state extend branch {
	    bid   INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    fid   INTEGER  NOT NULL  REFERENCES file,     -- File the item belongs to
	    lod   INTEGER            REFERENCES symbol,   -- Line of development (NULL => Trunk)
	    sid   INTEGER  NOT NULL  REFERENCES symbol,   -- Symbol capturing the branch

	    root  INTEGER            REFERENCES revision, -- Revision the branch sprouts from
	    first INTEGER            REFERENCES revision, -- First revision committed to the branch
	    bra   TEXT     NOT NULL,                      -- branch number
	    pos   INTEGER  NOT NULL                       -- creation order in root.

	    -- A branch can exist without root. It happens when the
            -- only revision on trunk is the unnecessary dead one the
            -- branch was sprouted from and it has commits. The branch
            -- will exist to be the LOD of its revisions, nothing to
            -- sprout from, the dead revision was removed, hence no
            -- root.
	} { root first sid }
	# Indices on: root  (revision successors)
	#             first (revision predecessors)
	#             sid   (tag predecessors, branch successors/predecessors)

	# Project level ...
	#	pLineOfDevelopment, pSymbol, pBranch, pTag, pTrunk
	#
	#	pTrunk  <- pLineOfDevelopment
	#	pBranch <- pSymbol, pLineOfDevelopment
	#	pTag    <- pSymbol, pLineOfDevelopment

	state extend symbol {
	    sid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    pid  INTEGER  NOT NULL  REFERENCES project,  -- Project the symbol belongs to
	    name TEXT     NOT NULL,
	    type INTEGER  NOT NULL  REFERENCES symtype,  -- enum { excluded = 0, tag, branch, undefined }

	    tag_count    INTEGER  NOT NULL, -- How often the symbol is used as tag.
	    branch_count INTEGER  NOT NULL, -- How often the symbol is used as branch
	    commit_count INTEGER  NOT NULL, -- How often a file was committed on the symbol

	    UNIQUE (pid, name) -- Symbols are unique within the project
	}

	state extend blocker {
	    -- For each symbol we save which other symbols are
	    -- blocking its removal (if the user asks for it).

	    sid INTEGER  NOT NULL  REFERENCES symbol, --
	    bid INTEGER  NOT NULL  REFERENCES symbol, -- Sprouted from sid, blocks it.
	    UNIQUE (sid, bid)
	}

	state extend parent {
	    -- For each symbol we save which other symbols can act as
	    -- a possible parent in some file, and how often.

	    sid INTEGER  NOT NULL  REFERENCES symbol, --
	    pid INTEGER  NOT NULL  REFERENCES symbol, -- Possible parent of sid
	    n   INTEGER  NOT NULL,                    -- How often pid can act as parent.
	    UNIQUE (sid, pid)
	}

	state extend symtype {
	    tid    INTEGER  NOT NULL  PRIMARY KEY,
	    name   TEXT     NOT NULL,
	    plural TEXT     NOT NULL,
	    UNIQUE (name)
	    UNIQUE (plural)
	}
	state run {
	    INSERT INTO symtype VALUES (0,'excluded', 'excluded');  -- The ids are the fixed
	    INSERT INTO symtype VALUES (1,'tag',      'tags');      -- pieces, see project::sym,
	    INSERT INTO symtype VALUES (2,'branch',   'branches');  -- getsymtypes and associated
	    INSERT INTO symtype VALUES (3,'undefined','undefined'); -- typevariables.
	}

	state extend meta {
	    -- Meta data of revisions. See revision.mid for the
	    -- reference. Many revisions can share meta data. This is
	    -- actually one of the criterions used to sort revisions
	    -- into changesets.

	    mid INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,

	    -- Meta data belongs to a specific project, stronger, to a
	    -- branch in that project. It further has a log message,
	    -- and its author. This is unique with the project and
	    -- branch.

	    pid INTEGER  NOT NULL  REFERENCES project,  --
	    bid INTEGER  NOT NULL  REFERENCES symbol,   --
	    aid INTEGER  NOT NULL  REFERENCES author,   --
	    cid INTEGER  NOT NULL  REFERENCES cmessage, --

	    UNIQUE (pid, bid, aid, cid)

	    -- Constraints: The project of the meta data of a revision
	    --              X is the same as the project of X itself.
	    --
	    -- ............ The branch of the meta data of a revision
	    --              X is the same as the line of development
	    --              of X itself.
	}

	# Authors and commit messages are fully global, i.e. per
	# repository.

	state extend author {
	    aid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT, -- Pool of the unique
	    name TEXT     NOT NULL  UNIQUE                      -- author names.
	}

	state extend cmessage {
	    cid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT, -- Pool of the unique
	    text TEXT     NOT NULL  UNIQUE                      -- log messages
	}

	project::sym getsymtypes
	file::rev    getopcodes
	return
    }

    typemethod load {} {
	state use symbol
	state use symtype
	state use optype

	project::sym getsymtypes
	file::rev    getopcodes
	repository   loadsymbols
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	set rbase [repository base?]
	foreach project [repository projects] {
	    set base [::file join $rbase [$project base]]
	    log write 1 collrev "Processing $base"

	    foreach file [$project files] {
		set path [$file path]
		log write 2 collrev "Parsing $path"
		if {[catch {
		    parser process [::file join $base $path] $file
		} msg]} {
		    global errorCode
		    if {$errorCode eq "vc::rcs::parser"} {
			trouble fatal "$path is not a valid RCS archive ($msg)"
		    } else {
			global errorInfo
			trouble internal $errorInfo
		    }
		} else {
		    # We persist the core of the data collected about
		    # each file immediately after it has been parsed
		    # and wrangled into shape, and then drop it from
		    # memory. This is done to keep the amount of
		    # required memory within sensible limits. Without
		    # doing it this way we would easily gobble up 1G
		    # of RAM or more with all the objects (revisions
		    # and file-level symbols).

		    $file persist
		}

		$file drop
	    }

	    $project purgeghostsymbols
	}

	repository persistrev
	repository printrevstatistics
	integrity  strict

	log write 1 collrev "Scan completed"
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard revision
	state discard tag
	state discard branch
	state discard symbol
	state discard blocker
	state discard parent
	state discard symtype
	state discard meta
	state discard author
	state discard cmessage
	return
    }

    # TODO: Move this code to the integrity module
    proc Paranoia {} {
	# This code performs a number of paranoid checks of the
	# database, searching for inconsistent cross-references.
	log write 4 collrev {Check database consistency}

	set n 0 ; # Counter for the checks (we print an id before the
		  # main label).

	# Find all revisions which disagree with their line of
	# development about the project they are owned by.
	Check \
	    {Revisions and their LODs have to be in the same project} \
	    {disagrees with its LOD about owning project} {
		SELECT F.name, R.rev
		FROM   revision R, file F, symbol S
		WHERE  R.fid = F.fid   -- Get file of revision
		AND    R.lod = S.sid   -- Get symbol for revision's LOD
		AND    F.pid != S.pid  -- but symbol is for a different project
		;
	    }
	# Find all revisions which disgree with their meta data about
	# the project they are owned by.
	Check \
	    {Revisions and their meta data have to be in the same project} \
	    {disagrees with its meta data about owning project} {
		SELECT F.name, R.rev
		FROM   revision R, file F, meta M
		WHERE  R.fid = F.fid   -- Get file of revision
		AND    R.mid = M.mid   -- Get meta data of revision
		AND    F.pid != M.pid  -- but is for a different project
		;
	    }
	# Find all revisions which disgree with their meta data about
	# the branch/line of development they belong to.
	Check \
	    {Revisions and their meta data have to be in the same LOD} \
	    {disagrees with its meta data about owning LOD} {
		SELECT F.name, R.rev
		FROM   revision R, meta M, file F
		WHERE  R.mid = M.mid   -- Get meta data of revision
		AND    R.lod != M.bid  -- but is for a different LOD
		AND    R.fid = F.fid   -- Get file of revision
		;
	    }
	# Find all revisions with a primary child which disagrees
	# about the file they belong to.
	Check \
	    {Revisions and their primary children have to be in the same file} \
	    {disagrees with its primary child about the owning file} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid        -- Get file of revision
		AND    R.child IS NOT NULL  -- Restrict to non-leaf revisions
		AND    R.child = C.rid      -- Get child (has to exist)
		AND    C.fid != R.fid       -- Whic wrongly is in a different file
		;
	    }

	# Find all revisions with a branch parent symbol whose parent
	# disagrees about the file they belong to.
	Check \
	    {Revisions and their branch children have to be in the same file} \
	    {at the beginning of its branch and its parent disagree about the owning file} {
		SELECT F.name, R.rev
		FROM   revision R, revision P, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.bparent IS NOT NULL  -- Restrict to first on branch
		AND    R.parent = P.rid       -- Get out-of-branch parent
		AND    R.fid != P.fid         -- Which wrongly is in a different file
		;
	    }
	# Find all revisions with a non-NTDB child which disagrees
	# about the file they belong to.
	Check \
	    {Revisions and their non-NTDB children have to be in the same file} \
	    {disagrees with its non-NTDB child about the owning file} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.dbchild IS NOT NULL  -- Restrict to semi-last NTDB revision
		AND    R.dbchild = C.rid      -- Got to associated trunk revision
		AND    C.fid != R.fid         -- Which wrongly is in a different file
		;
	    }
	# Find all revisions which have a primary child, but the child
	# does not have them as parent.
	Check \
	    {Revisions have to be parents of their primary children} \
	    {is not the parent of its primary child} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid        -- Get file of revision
		AND    R.child IS NOT NULL  -- Restrict to non-leaves
		AND    R.child = C.rid      -- Get the child (has to exist)
		AND    C.parent != R.rid    -- Which does not have us as its parent.
		;
	    }
	# Find all revisions which have a primrary child, but the
	# child has a branch parent symbol making them brach starters.
	Check \
	    {Primary children of revisions must not start branches} \
	    {is parent of a primary child which is the beginning of a branch} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid           -- Get file of revision
		AND    R.child IS NOT NULL     -- Restrict to non-leaves
		AND    R.child = C.rid         -- Get the child (has to exist)
		AND    C.bparent IS NOT NULL   -- wrongly claiming to be first on branch
		;
	    }
	# Find all revisions without branch parent symbol which have a
	# parent, but the parent does not have them as primary child.
	Check \
	    {Revisions have to be primary children of their parents, if any} \
	    {is not the child of its parent} {
		SELECT F.name, R.rev
		FROM   revision R, revision P, file F
		WHERE  R.fid = F.fid
		AND    R.bparent IS NULL     -- Get file of revision
		AND    R.parent IS NOT NULL  -- Restrict to everything not first on a branch
		AND    R.parent = P.rid      -- Get the parent (has to exist)
		AND    P.child != R.rid      -- Which do not have us as their child
		;
	    }
	# Find all revisions with a branch parent symbol which do not
	# have a parent.
	Check \
	    {Branch starting revisions have to have a parent} \
	    {at the beginning of its branch has no parent} {
		SELECT F.name, R.rev
		FROM   revision R, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.bparent IS NOT NULL  -- Restrict to first on a branch
		AND    R.parent IS NULL       -- But there is no out-of-branch parent
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# has them as primary child.
	Check \
	    {Branch starting revisions must not be primary children of their parents} \
	    {at the beginning of its branch is the primary child of its parent} {
		SELECT F.name, R.rev
		FROM   revision R, revision P, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.bparent IS NOT NULL  -- Restrict to first on a branch
		AND    R.parent IS NOT NULL   -- Which are not detached
		AND    R.parent = P.rid       -- Get their non-branch parent
		AND    P.child = R.rid        -- which improperly has them as primary child
		;
	    }
	# Find all revisions with a non-NTDB child which are not on
	# the NTDB.
	Check \
	    {NTDB to trunk transition has to begin on NTDB} \
	    {has a non-NTDB child, yet is not on the NTDB} {
		SELECT F.name, R.rev
		FROM   revision R, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.dbchild IS NOT NULL  -- Restrict to semi-last NTDB revision
		AND    NOT R.isdefault        -- Improperly claiming to not be on NTDB
		;
	    }
	# Find all revisions with a NTDB parent which are on the NTDB.
	Check \
	    {NTDB to trunk transition has to end on non-NTDB} \
	    {has a NTDB parent, yet is on the NTDB} {
		SELECT F.name, R.rev
		FROM   revision R, file F
		WHERE  R.fid = F.fid           -- Get file of revision
		AND    R.dbparent IS NOT NULL  -- Restrict to trunk roots with NTDB around
		AND    R.isdefault             -- But root improperly claims to be on NTDB
		;
	    }
	# Find all revisions with a child which disagrees about the
	# line of development they belong to.
	Check \
	    {Revisions and their primary children have to be in the same LOD} \
	    {and its primary child disagree about their LOD} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid        -- Get file of revision
		AND    R.child IS NOT NULL  -- Restrict to non-leaves
		AND    R.child = C.rid      -- Get child (has to exist)
		AND    C.lod != R.lod       -- which improperly uses a different LOD
		;
	    }
	# Find all revisions with a non-NTDB child which agrees about
	# the line of development they belong to.
	Check \
	    {NTDB and trunk revisions have to be in different LODs} \
	    {on NTDB and its non-NTDB child wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM   revision R, revision C, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.dbchild IS NOT NULL  -- Restrict to semi-last NTDB revision
		AND    R.dbchild = C.rid      -- Get associated trunk root revision
		AND    C.lod = R.lod          -- Improperly uses the same LOD
		;
	    }
	# Find all revisions with a branch parent symbol which is not
	# their LOD.
	Check \
	    {Branch starting revisions have to have their LOD as branch parent symbol} \
	    {at the beginning of its branch does not have the branch symbol as its LOD} {
		SELECT F.name, R.rev
		FROM   revision R, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.bparent IS NOT NULL  -- Restrict to revisions first on a branch
		AND    R.lod != R.bparent     -- and their branch is not their LOD
		;
	    }
	# Find all revisions with a branch parent symbol whose parent
	# is in the same line of development.
	Check \
	    {Revisions and their branch children have to be in different LODs} \
	    {at the beginning of its branch and its parent wrongly agree about their LOD} {
		SELECT F.name, R.rev
		FROM   revision R, revision P, file F
		WHERE  R.fid = F.fid          -- Get file of revision
		AND    R.bparent IS NOT NULL  -- Restrict to revisions first on a branch
		AND    R.parent = P.rid       -- Get their non-branch parent
		AND    R.lod = P.lod          -- Which improperly uses the same LOD
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
	log write 5 collrev "\[[format %02d [incr n]]\] [expr {$ok ? "Ok    " : "Failed"}] ... $header"
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::pass {
    namespace export collrev
    namespace eval collrev {
	namespace import ::vc::rcs::parser
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	namespace eval file {
	    namespace import ::vc::fossil::import::cvs::file::rev
	}
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collrev
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collrev 1.0
return
