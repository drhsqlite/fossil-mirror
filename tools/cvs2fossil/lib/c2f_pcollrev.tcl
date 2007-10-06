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
package require vc::fossil::import::cvs::state      ; # State storage

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
	#	Event, Revision, Symbol, Branch, Tag
	#
	#	Tag    <- Symbol <- Event
	#	Branch <- Symbol <- Event
	#	Revision <- Event
	#
	#	Head revision, Principal branch, Comment

	state writing rcs {
	    fid       INTEGER  NOT NULL  REFERENCES file,    -- RCS inherit from FILE
	    head      INTEGER  NOT NULL  REFERENCES revision,
	    principal INTEGER  NOT NULL  REFERENCES branch,
	    comment   TEXT     NOT NULL
	}

	state writing item {
	    iid  INTEGER  NOT NULL  PRIMARY KEY AUTOINCREMENT,
	    type INTEGER  NOT NULL,                 -- enum { tag = 1, branch, revision }
	    fid  INTEGER  NOT NULL  REFERENCES file  -- File the item belongs to
	}

	state writing revision {
	    iid   INTEGER  NOT NULL  REFERENCES item,   -- REVISION inherit from ITEM
	    lod   INTEGER  NOT NULL  REFERENCES symbol, -- Line of development

	    -- The tags and branches belonging to a revision can be
	    -- determined by selecting on the backreferences in the
	    -- tag and branch tables.

	    rev   TEXT     NOT NULL,                     -- revision number
	    date  INTEGER  NOT NULL,                     -- date of entry, seconds since epoch
	    state TEXT     NOT NULL,                     -- state of revision
	    mid   INTEGER  NOT NULL REFERENCES meta,     -- meta data (author, commit message)
	    next  INTEGER  NOT NULL REFERENCES revision, -- next in chain of revisions.
	    cs    INTEGER  NOT NULL, -- Revision content as offset and length
	    cl    INTEGER  NOT NULL  -- into the archive file.
	}

	state writing tag {
	    iid INTEGER  NOT NULL  REFERENCES item,     -- TAG inherit from ITEM
	    sid INTEGER  NOT NULL  REFERENCES symbol,   -- Symbol capturing the tag
	    rev INTEGER  NOT NULL  REFERENCES revision -- The revision being tagged.
	}

	state writing branch {
	    iid   INTEGER  NOT NULL  REFERENCES item,     -- BRANCH inherit from ITEM
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
	    mid INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    pid INTEGER  NOT NULL  REFERENCES project,  -- project the commit was on
	    bid INTEGER  NOT NULL  REFERENCES symbol,   -- branch the commit was on
	    aid INTEGER  NOT NULL  REFERENCES author,
	    cid INTEGER  NOT NULL  REFERENCES cmessage,

	    UNIQUE (pid, bid, aid, cid)
	}

	state writing author {
	    aid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    name TEXT     NOT NULL  UNIQUE
	}

	state writing cmessage {
	    cid  INTEGER  NOT NULL  PRIMARY KEY  AUTOINCREMENT,
	    text TEXT     NOT NULL  UNIQUE
	}

	# Consistency constraints.
	#
	# Items (Tags, Branches, Revisions) belong to a file to a
	# project. All refer to other items, and symbols, which again
	# belong to a project. The projects have to agree with each
	# other. I.e. items may not refer to items or symbols which
	# belong to a different project than their own.

	return
    }

    typemethod run {} {
	log write 1 collrev "Scan completed"
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
