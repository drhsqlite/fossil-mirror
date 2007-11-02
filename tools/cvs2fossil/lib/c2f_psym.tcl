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

## Symbols (Tags, Branches) per project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::tools::misc                       ; # Text formatting.
package require vc::fossil::import::cvs::state        ; # State storage.
package require struct::set                           ; # Set handling.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {name id project} {
	set myname    $name
	set myid      $id
	set myproject $project

	# Count total number of symbols.
	incr mynum
	return
    }

    method name {} { return $myname }
    method id   {} { return $myid   }

    # # ## ### ##### ######## #############
    ## Symbol type

    method determinetype {} {
	# This is done by a fixed heuristics, with guidance by the
	# user in edge-cases. Contrary to cvs2svn which uses a big
	# honking streagy class and rule objects. Keep it simple, we
	# can expand later when we actually need all the complexity
	# for configurability.

	# The following guidelines are applied:
	# - Is usage unambigous ?
	# - Was there ever a commit on the symbol ?
	# - More used as tag, or more used as branch ?
	# - At last, what has the user told us about it ?
	# - Fail
	
	foreach rule {
	    UserConfig
	    Unambigous
	    HasCommits
	    VoteCounts
	} {
	   set chosen [$self $rule]
	   if {$chosen eq $myundef} continue
	   $self MarkAs $rule $chosen
	   return
	}

	# None of the above was able to decide which type to assign to
	# the symbol. This is a fatal error preventing the execution
	# of the passes after 'CollateSymbols'.

	incr myrulecount(Undecided_)
	trouble fatal "Unable to decide how to convert symbol '$myname'"
	return
    }

    method markthetrunk {} { $self MarkAs IsTheTrunk $mybranch ; return }

    # # ## ### ##### ######## #############
    ## Symbol statistics

    method defcounts {tc bc cc} {
	set mybranchcount $tc
	set mytagcount    $bc
	set mycommitcount $cc
	return
    }

    method countasbranch {} { incr mybranchcount ; return }
    method countastag    {} { incr mytagcount    ; return }
    method countacommit  {} { incr mycommitcount ; return }

    method blockedby {symbol} {
	# Remember the symbol as preventing the removal of this
	# symbol. Ot is a tag or branch that spawned from a revision
	# on this symbol.

	struct::set include myblockers $symbol
	return
    }

    method possibleparent {symbol} {
	if {[info exists mypparent($symbol)]} {
	    incr mypparent($symbol)
	} else {
	    set  mypparent($symbol) 1
	}
	return
    }

    method isghost {} {
	# Checks if this symbol (as line of development) never
	# existed.

	if {$mycommitcount > 0}         { return 0 }
	if {[llength $myblockers]}      { return 0 }
	if {[array size mypparent] > 0} { return 0 }

	return 1
    }

    # # ## ### ##### ######## #############

    method persistrev {} {
	set pid [$myproject id]

	state transaction {
	    state run {
		INSERT INTO symbol ( sid,   pid,  name,   type,     tag_count,   branch_count,   commit_count)
		VALUES             ($myid, $pid, $myname, $myundef, $mytagcount, $mybranchcount, $mycommitcount);
	    }
	    foreach symbol $myblockers {
		set bid [$symbol id]
		state run {
		    INSERT INTO blocker (sid,   bid)
		    VALUES              ($myid, $bid);
		}
	    }
	    foreach {symbol count} [array get mypparent] {
		set pid [$symbol id]
		state run {
		    INSERT INTO parent (sid,   pid,  n)
		    VALUES             ($myid, $pid, $count);
		}
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myproject {} ; # Reference to the project object
			    # containing the symbol.
    variable myname    {} ; # The symbol's name
    variable myid      {} ; # Repository wide numeric id of the
			    # symbol. This implicitly encodes the
			    # project as well.

    variable mybranchcount 0 ; # Count how many uses as branch.
    variable mytagcount    0 ; # Count how many uses as tag.
    variable mycommitcount 0 ; # Count how many files did a commit on the symbol.

    variable myblockers   {} ; # List (Set) of the symbols which block
			       # the exclusion of this symbol.

    variable mypparent -array {} ; # Maps from symbols to the number
				   # of files in which it could have
				   # been a parent of this symbol.

    variable mytype {} ; # The type chosen for the symbol to use in
			 # the conversion.

    # # ## ### ##### ######## #############

    typemethod getsymtypes {} {
	foreach {tid name} [state run {
	    SELECT tid, name FROM symtype;
	}] { set mysymtype($tid) $name }
	return
    }

    # Keep the codes below in sync with 'pass::collrev/setup('symtype').
    typevariable myexcluded        0 ; # Code for symbols which are excluded.
    typevariable mytag             1 ; # Code for symbols which are tags.
    typevariable mybranch          2 ; # Code for symbols which are branches.
    typevariable myundef           3 ; # Code for symbols of unknown type.
    typevariable mysymtype -array {} ; # Map from type code to label for the log.

    typemethod printrulestatistics {} {
	log write 2 symbol "Rule usage statistics:"

	set fmt %[string length $mynum]s
	set all 0

	foreach key [lsort [array names myrulecount]] {
	    log write 2 symbol "* [format $fmt $myrulecount($key)] $key"
	    incr all $myrulecount($key)
	}

	log write 2 symbol "= [format $fmt $all] total"
	return
    }

    # Statistics on how often each 'rule' was used to decide on the
    # type of a symbol.
    typevariable myrulecount -array {
	HasCommits 0
	IsTheTrunk 0
	Unambigous 0
	Undecided_ 0
	UserConfig 0
	VoteCounts 0
    }

    typemethod printtypestatistics {} {
	log write 2 symbol "Symbol type statistics:"

	set fmt %[string length $mynum]s
	set all 0

	foreach {stype splural n} [state run {
	    SELECT T.name, T.plural, COUNT (s.sid)
	    FROM symbol S, symtype T
	    WHERE S.type = T.tid
	    GROUP BY T.name
	    ORDER BY T.name
	    ;
	}] {
	    log write 2 symbol "* [format $fmt $n] [sp $n $stype $splural]"
	    incr all $n
	}

	log write 2 symbol "= [format $fmt $all] total"
	return
    }

    typevariable mynum 0

    # # ## ### ##### ######## #############
    ## Internal methods

    method UserConfig {} {
	# No user based guidance yet.
	return $myundef
    }

    method Unambigous {} {
	# If a symbol is used unambiguously as a tag/branch, convert
	# it as such.

	set istag    [expr {$mytagcount    > 0}]	
	set isbranch [expr {$mybranchcount > 0 || $mycommitcount > 0}]

	if {$istag && $isbranch} { return $myundef  }
	if {$istag}              { return $mytag    }
	if {$isbranch}           { return $mybranch }

	# Symbol was not used at all.
	return $myundef
    }

    method HasCommits {} {
	# If there was ever a commit on the symbol, convert it as a
	# branch.

	if {$mycommitcount > 0} { return $mybranch }
	return $myundef
    }

    method VoteCounts {} {
	# Convert the symbol based on how often it was used as a
	# branch/tag. Whichever happened more often determines how the
	# symbol is converted.

	if {$mytagcount > $mybranchcount} { return $mytag }
	if {$mytagcount < $mybranchcount} { return $mybranch }
	return $myundef
    }

    method MarkAs {label chosen} {
	log write 3 symbol "\[$label\] Converting symbol '$myname' as $mysymtype($chosen)"

	set mytype $chosen
	incr myrulecount($label)

	# This is stored directly into the database.
	state run {
	    UPDATE symbol
	    SET type = $chosen
	    WHERE sid = $myid
	    ;
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export sym
    namespace eval sym {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register symbol
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::sym 1.0
return
