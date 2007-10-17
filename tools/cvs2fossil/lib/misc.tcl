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

## Utilities for various things: text formatting, max, ...

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4 ; # Required runtime

# # ## ### ##### ######## ############# #####################
## 

namespace eval ::vc::tools::misc {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    # Choose singular vs plural forms of a word based on a number.

    proc sp {n singular {plural {}}} {
	if {$n == 1} {return $singular}
	if {$plural eq ""} {set plural ${singular}s}
	return $plural
    }

    # As above, with the number automatically put in front of the
    # string.

    proc nsp {n singular {plural {}}} {
	return "$n [sp $n $singular $plural]"
    }

    # Find maximum in a list.

    proc max {list} {
	set max -1
	foreach e $list {
	    if {$e < $max} continue
	    set max $e
	}
	return $max
    }

    proc ldelete {lv item} {
	upvar 1 $lv list
	set pos [lsearch -exact $list $item]
	if {$pos < 0} return
	set list [lreplace $list $pos $pos]
	return
    }

    # Delete item from list by name

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools::misc {
    namespace export sp nsp max ldelete
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::misc 1.0
return
