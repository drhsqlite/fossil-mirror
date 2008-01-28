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

    # Find maximum/minimum in a list.

    proc max {list} {
	set max -1
	foreach e $list {
	    if {$e < $max} continue
	    set max $e
	}
	return $max
    }

    proc min {list} {
	set min {}
	foreach e $list {
	    if {$min == {}} {
		set min $e
	    } elseif {$e > $min} continue
	    set min $e
	}
	return $min
    }

    proc max2 {a b} {
	if {$a > $b}  { return $a }
	return $b
    }

    proc min2 {a b} {
	if {$a < $b}  { return $a }
	return $b
    }

    proc ldelete {lv item} {
	upvar 1 $lv list
	set pos [lsearch -exact $list $item]
	if {$pos < 0} return
	set list [lreplace $list $pos $pos]
	return
    }

    # Delete item from list by name

    proc striptrailingslash {path} {
	# split and rejoin gets rid of a traling / character.
	return [eval [linsert [file split $path] 0 ::file join]]
    }

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools::misc {
    namespace export sp nsp max min max2 min2 ldelete striptrailingslash
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::misc 1.0
return
