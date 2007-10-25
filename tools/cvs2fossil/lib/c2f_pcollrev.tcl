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

## Pass II. This pass parses the colected rcs archives and extracts
## all the information they contain (revisions, and symbols).

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require fileutil::traverse                  ; # Directory traversal.
package require fileutil                            ; # File & path utilities.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::log                      ; # User feedback.
package require vc::fossil::import::cvs::pass       ; # Pass management.
package require vc::fossil::import::cvs::repository ; # Repository management.
package require vc::fossil::import::cvs::state      ; # State storage.
package require vc::rcs::parser                     ; # Rcs archive data extraction.

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

	state reading project
	state reading file

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

	state writing revision {
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
	    -- epoch, for easy comparison. The text content is an
	    -- (offset,length) pair into the rcs archive.

	    op    INTEGER  NOT NULL,
	    date  INTEGER  NOT NULL,
	    state TEXT     NOT NULL,
	    mid   INTEGER  NOT NULL REFERENCES meta,
	    coff  INTEGER  NOT NULL,
	    clen  INTEGER  NOT NULL,

	    UNIQUE (fid, rev) -- The DTN is unique within the revision's file.
	}

	state writing tag {
	    tid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    fid  INTEGER  NOT NULL  REFERENCES file,     -- File the item belongs to
	    lod  INTEGER            REFERENCES symbol,   -- Line of development (NULL => Trunk)

	    sid  INTEGER  NOT NULL  REFERENCES symbol,   -- Symbol capturing the tag

	    rev  INTEGER  NOT NULL  REFERENCES revision  -- The revision being tagged.
	}

	state writing branch {
	    bid   INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    fid   INTEGER  NOT NULL  REFERENCES file,     -- File the item belongs to
	    lod   INTEGER            REFERENCES symbol,   -- Line of development (NULL => Trunk)

	    sid   INTEGER  NOT NULL  REFERENCES symbol,   -- Symbol capturing the branch

	    root  INTEGER  NOT NULL  REFERENCES revision, -- Revision the branch sprouts from
	    first INTEGER            REFERENCES revision, -- First revision committed to the branch
	    bra   TEXT     NOT NULL                       -- branch number
	}

	# It is in principle possible to collapse the four tables
	# above (from item to barnch) into a single table, with
	# similar columns merged, and unused columns allowing NULL,
	# the use determined by the type. We may do that if the
	# performance is not good enough, but for now clarity of
	# structure over efficiency.

	# Project level ...
	#	pLineOfDevelopment, pSymbol, pBranch, pTag, pTrunk
	#
	#	pTrunk  <- pLineOfDevelopment
	#	pBranch <- pSymbol, pLineOfDevelopment
	#	pTag    <- pSymbol, pLineOfDevelopment

	state writing symbol {
	    sid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    pid  INTEGER  NOT NULL  REFERENCES project,  -- Project the symbol belongs to
	    name TEXT     NOT NULL,
	    type INTEGER  NOT NULL,                      -- enum { tag = 1, branch, undefined }

	    tag_count    INTEGER  NOT NULL, -- How often the symbol is used as tag.
	    branch_count INTEGER  NOT NULL, -- How often the symbol is used as branch
	    commit_count INTEGER  NOT NULL, -- How often a file was committed on the symbol

	    UNIQUE (pid, name) -- Symbols are unique within the project
	}

	state writing blocker {
	    sid INTEGER  NOT NULL  REFERENCES symbol, -- 
	    bid INTEGER  NOT NULL  REFERENCES symbol, -- Sprouted from sid, blocks it.
	    UNIQUE (sid, bid)
	}

	state writing parent {
	    sid INTEGER  NOT NULL  REFERENCES symbol, -- 
	    pid INTEGER  NOT NULL  REFERENCES symbol, -- Possible parent of sid
	    UNIQUE (sid, pid)
	}

	state writing meta {
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

	state writing author {
	    aid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    name TEXT     NOT NULL  UNIQUE
	}

	state writing cmessage {
	    cid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    text TEXT     NOT NULL  UNIQUE
	}

	return
    }

    typemethod load {} {
	# TODO
	return
    }

    typemethod run {} {
	# Pass manager interface. Executed to perform the
	# functionality of the pass.

	set rbase [repository base?]
	foreach project [repository projects] {
	    set base [file join $rbase [$project base]]
	    log write 1 collrev "Processing $base"

	    foreach file [$project files] {
		set path [$file path]
		log write 2 collrev "Parsing $path"
		if {[catch {
		    parser process [file join $base $path] $file
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
	}

	repository printrevstatistics
	repository persistrev

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
	state discard meta
	state discard author
	state discard cmessage
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
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collrev
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collrev 1.0
return
