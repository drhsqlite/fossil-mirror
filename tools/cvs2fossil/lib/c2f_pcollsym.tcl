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

## Pass III. This pass divides the symbols collected by the previous
## pass into branches, tags, and excludes. The latter are also
## partially deleted by this pass, not only marked. It is the next
## pass however, 'FilterSym', which performs the full deletion.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::repository   ; # Repository management.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::project::sym ; # Project level symbols

# # ## ### ##### ######## ############# #####################
## Register the pass with the management

vc::fossil::import::cvs::pass define \
    CollateSymbols \
    {Collate symbols} \
    ::vc::fossil::import::cvs::pass::collsym

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass::collsym {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod setup {} {
	# Define names and structure of the persistent state of this
	# pass.

	state use project
	state use symbol
	state use symtype
	state use blocker
	state use parent

	state extend preferedparent {
	    -- For each symbol the prefered parent. This describes the
	    -- tree of the found lines of development. Actually a
	    -- forest in case of multiple projects, with one tree per
	    -- project.

	    sid INTEGER  NOT NULL  PRIMARY KEY  REFERENCES symbol,
	    pid INTEGER  NOT NULL               REFERENCES symbol
	} { pid }
	# Index on: pid (branch successors)
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

	state transaction {
	    repository   determinesymboltypes

	    project::sym printrulestatistics
	    project::sym printtypestatistics
	}

	if {![trouble ?]} {
	    UnconvertedSymbols
	    BadSymbolTypes
	    BlockedExcludes
	    InvalidTags
	}

	if {![trouble ?]} {
	    DropExcludedSymbolsFromReferences
	    DeterminePreferedParents
	}

	log write 1 collsym "Collation completed"
	return
    }

    typemethod discard {} {
	# Pass manager interface. Executed for all passes after the
	# run passes, to remove all data of this pass from the state,
	# as being out of date.

	state discard preferedparent
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    ## TODO: Move UnconvertedSymbols, BadSymbolTypes, BlockedIncludes,
    ## TODO: InvalidTags to the integrity module?

    proc UnconvertedSymbols {} {
	# Paranoia - Have we left symbols without conversion
	# information (i.e. with type 'undefined') ?

	set undef [project::sym undef]

	state foreachrow {
	    SELECT P.name AS pname, S.name AS sname
	    FROM   symbol S, project P
	    WHERE  S.type = $undef  -- Restrict to undefined symbols
	    AND    P.pid = S.pid    -- Get project for symbol
	} {
	    trouble fatal "$pname : The symbol '$sname' was left undefined"
	}
	return
    }

    proc BadSymbolTypes {} {
	# Paranoia - Have we left symbols with bogus conversion
	# information (type out of the valid range (excluded, branch,
	# tag)) ?

	state foreachrow {
	    SELECT P.name AS pname, S.name AS sname
	    FROM   symbol S, project P
	    WHERE  S.type NOT IN (0,1,2) -- Restrict to symbols with bogus type codes
	    AND    P.pid = S.pid         -- Get project of symbol
	} {
	    trouble fatal "$pname : The symbol '$sname' has no proper conversion type"
	}
	return
    }

    proc BlockedExcludes {} {
	# Paranoia - Have we scheduled symbols for exclusion without
	# also excluding their dependent symbols ?

	set excl [project::sym excluded]

	state foreachrow {
	    SELECT P.name AS pname, S.name AS sname, SB.name AS bname
	    FROM   symbol S, blocker B, symbol SB, project P
	    WHERE  S.type = $excl   -- Restrict to excluded symbols
	    AND    S.sid = B.sid    -- Get symbols blocking them
	    AND    B.bid = SB.sid   -- and
	    AND    SB.type != $excl -- which are not excluded themselves
	    AND    P.pid = S.pid    -- Get project of symbol
	} {
	    trouble fatal "$pname : The symbol '$sname' cannot be excluded as the unexcluded symbol '$bname' depends on it."
	}
	return
    }

    proc InvalidTags {} {
	# Paranoia - Have we scheduled symbols for conversion as tags
	# which absolutely cannot be converted as tags due to commits
	# made on them ?

	# In other words, this checks finds out if the user has asked
	# nonsensical conversions of symbols, which should have been
	# left to the heuristics, most specifically
	# 'project::sym.HasCommits()'.

	set tag [project::sym tag]

	state foreachrow {
	    SELECT P.name AS pname, S.name AS sname
	    FROM   project P, symbol S
	    WHERE  S.type = $tag        -- Restrict to tag symbols
	    AND    S.commit_count > 0   -- which have revisions committed to them
	    AND    P.pid = S.pid        -- Get project of symbol
	} {
	    trouble fatal "$pname : The symbol '$sname' cannot be forced to be converted as tag because it has commits."
	}
	return
    }

    proc DropExcludedSymbolsFromReferences {} {
	# The excluded symbols cann be used as blockers nor as
	# possible parent for other symbols. We now drop the relevant
	# entries to prevent them from causing confusion later on.

	set excl [project::sym excluded]

	state run {
	    DELETE FROM blocker
	    WHERE bid IN (SELECT sid
			  FROM   symbol
			  WhERE  type = $excl); -- Get excluded symbols
	    DELETE FROM parent
	    WHERE pid IN (SELECT sid
			  FROM   symbol
			  WhERE  type = $excl); -- Get excluded symbols
	}
	return
    }

    proc DeterminePreferedParents {} {
	array set prefered {}

	set excl [project::sym excluded]

	# Phase I: Pull the possible parents, using sorting to put the
	#          prefered parent of each symbol last among all
	#          candidates, allowing us get the prefered one by
	#          each candidate overwriting all previous
	#          selections. Note that we ignore excluded symbols,
	#          we do not care about their prefered parents and do
	#          not attempt to compute them.

	state foreachrow {
	    SELECT   S.sid   AS xs,
	             P.pid   AS xp,
	             S.name  AS sname,
	             SB.name AS pname,
	             PR.name AS prname,
	             P.n     AS votes
	    FROM     symbol S, parent P, symbol SB, project PR
	    WHERE    S.type != $excl      -- Restrict to wanted symbols
	    AND      S.sid = P.sid        -- Get possible parents of symbol
	    AND      P.pid = SB.sid       -- and
	    AND      S.pid = PR.pid       -- the project of the symbol
	    ORDER BY P.n ASC, P.pid DESC  -- Sorting, see below
	    --
	    -- Higher votes and smaller ids (= earlier branches) last
	    -- We simply keep the last possible parent for each
	    -- symbol.  This parent will have the max number of votes
	    -- for its symbol and will be the earliest created branch
	    -- possible among all with many votes.
	} {
	    log write 9 pcollsym "Voting $votes for Parent($sname) = $pname"

	    set prefered($xs) [list $xp $sname $pname $prname]
	}

	# Phase II: Write the found preferences back into the table
	#           this pass defined for it.

	foreach {s x} [array get prefered] {
	    struct::list assign $x p sname pname prname
	    state run {
		INSERT INTO preferedparent (sid, pid)
		VALUES                     ($s,  $p);
	    }

	    log write 3 pcollsym "$prname : '$sname's prefered parent is '$pname'"
	}

	# Phase III: Check the result that all symbols except for
	#            trunks have a prefered parent. We also ignore
	#            excluded symbols, as we intentionally did not
	#            compute a prefered parent for them, see phase I.

	state foreachrow {
	    SELECT PR.name AS pname, S.name AS sname
	    FROM   symbol S LEFT OUTER JOIN preferedparent P
	    ON     S.sid = P.sid,       -- From symbol to prefered parent
	           project PR
	    WHERE  P.pid IS NULL        -- restrict to symbols without a preference
	    AND    S.type != $excl      -- which are not excluded
	    AND    S.name != ':trunk:'  -- and are not a trunk
	    AND    S.pid = PR.pid       -- get project of symbol
	} {
	    trouble fatal "$pname : '$sname' has no prefered parent."
	}

	# The reverse, having prefered parents for unknown symbols
	# cannot occur.
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
    namespace export collsym
    namespace eval collsym {
	namespace import ::vc::fossil::import::cvs::repository
	namespace import ::vc::fossil::import::cvs::state
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::sym
	}
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register collsym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass::collsym 1.0
return
