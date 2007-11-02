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

package require Tcl 8.4                                 ; # Required runtime.
package require snit                                    ; # OO system.
package require struct::set                             ; # Set handling.
package require vc::fossil::import::cvs::state          ; # State storage.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {name id project} {
	set myname    $name
	set myid      $id
	set myproject $project
	return
    }

    method name {} { return $myname }
    method id   {} { return $myid   }

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

    # Keep the codes below in sync with 'pass::collrev/setup('symtype').
    typevariable myexcluded 0 ; # Code for symbols which are excluded.
    typevariable mytag      1 ; # Code for symbols which are tags.
    typevariable mybranch   2 ; # Code for symbols which are branches.
    typevariable myundef    3 ; # Code for symbols of unknown type.

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export sym
    namespace eval sym {
	namespace import ::vc::fossil::import::cvs::state
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::sym 1.0
return
