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

	state reading symbol
	state reading blocker
	state reading parent
	return
    }

    typemethod load {} {
	# TODO
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
	}

	log write 1 collsym "Collation completed"
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

    proc UnconvertedSymbols {} {
	# Paranoia - Have we left symbols without conversion
	# information (i.e. with type 'undefined') ?

	set undef [project::sym undef]

	foreach {pname sname} [state run {
	    SELECT P.name, S.name
	    FROM   project P, symbol S
	    WHERE  P.pid = S.pid
	    AND    S.type = $undef
	}] {
	    trouble fatal "$pname : The symbol '$sname' was left undefined"
	}
	return
    }

    proc BadSymbolTypes {} {
	# Paranoia - Have we left symbols with bogus conversion
	# information (type out of the valid range (excluded, branch,
	# tag)) ?

	foreach {pname sname} [state run {
	    SELECT P.name, S.name
	    FROM   project P, symbol S
	    WHERE  P.pid = S.pid
	    AND    S.type NOT IN (0,1,2)
	}] {
	    trouble fatal "$pname : The symbol '$sname' has no proper conversion type"
	}
	return
    }

    proc BlockedExcludes {} {
	# Paranoia - Have we scheduled symbols for exclusion without
	# also excluding their dependent symbols ?

	set excl [project::sym excluded]

	foreach {pname sname bname} [state run {
	    SELECT P.name, S.name, SB.name
	    FROM   project P, symbol S, blocker B, symbol SB
	    WHERE  P.pid = S.pid
	    AND    S.type = $excl
	    AND    S.sid = B.sid
	    AND    B.bid = SB.sid
	    AND    SB.type != $excl
	}] {
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

	foreach {pname sname} [state run {
	    SELECT P.name, S.name
	    FROM   project P, symbol S
	    WHERE  P.pid = S.pid
	    AND    S.type = $tag
	    AND    S.commit_count > 0
	}] {
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
			  WhERE  type = $excl);
	    DELETE FROM parent
	    WHERE pid IN (SELECT sid
			  FROM   symbol
			  WhERE  type = $excl);
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
