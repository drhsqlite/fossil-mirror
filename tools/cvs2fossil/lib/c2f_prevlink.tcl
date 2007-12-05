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

## Helper class for the pass 6 cycle breaker. Each instance refers to
## three changesets A, B, and C, with A a predecessor of B, and B
## predecessor of C, and the whole part of a dependency cycle.

## Instances analyse the file level dependencies which gave rise to
## the changeset dependencies of A, B, and C, with the results used by
## the cycle breaker algorithm to find a good location where to at
## least weaken and at best fully break the cycle.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::tools::misc                       ; # Text formatting
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.
package require vc::fossil::import::cvs::project::rev ; # Project level changesets

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::project::revlink {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {prev cset next} {
	set myprev $prev
	set mycset $cset
	set mynext $next

	# We perform the bulk of the analysis during construction. The
	# file revisions held by the changeset CSET can be sorted into
	# four categories.

	# 1. Revisions whose predecessors are not in PREV, nor are
	#    their successors found in NEXT. These revisions do not
	#    count, as they did not induce any of the two dependencies
	#    under consideration. They can be ignored.

	# 2. Revisions which have predecessors in PREV and successors
	#    in NEXT. They are called 'passthrough' in cvs2svn. They
	#    induce both dependencies under consideration and are thus
	#    critical in the creation of the cycle. As such they are
	#    also unbreakable :(

	# 3. Revisions which have predecessor in PREVE, but no
	#    successors in NEXT. As such they induced the incoming
	#    dependency, but not the outgoing one.

	# 4. Revisions which have no predecessors in PREVE, but their
	#    successors are in NEXT. As such they induced the outgoing
	#    dependency, but not the incoming one.

	# If we have no passthrough revisions then splitting the
	# changeset between categories 3 and 4, with category 1 going
	# wherever, will break the cycle. If category 2 revisions are
	# present we can still perform the split, this will however
	# not break the cycle, only weaken it.

	# NOTE: This is the only remaining user of 'nextmap'. Look
	# into the possibility of performing the relevant counting
	# within the database.

	array set csetprevmap [Invert [$myprev nextmap]]
	array set csetnextmap [$mycset nextmap]

	set prevrev [$myprev items]
	set nextrev [$mynext items]

	foreach item [$mycset items] {
	    set rt [RT $item]
	    incr    mycount($rt)
	    lappend mycategory($rt) $item
	}
	return
    }

    # Result is TRUE if and only breaking myset will do some good.
    method breakable {} { expr  {$mycount(prev) || $mycount(next)} }
    method passcount {} { return $mycount(pass) }

    method linkstomove {} {
	# Return the number of revisions that would be moved should we
	# split the changeset.

	set n [min2 $mycount(prev) $mycount(next)]
	if {$n > 0 } { return $n }
	return [max2 $mycount(prev) $mycount(next)]
    }

    method betterthan {other} {
	set sbreak [$self  breakable]
	set obreak [$other breakable]

	if {$sbreak && !$obreak} { return 1 } ; # self is better.
	if {!$sbreak && $obreak} { return 0 } ; # self is worse.

	# Equality. Look at the counters.
	# - Whichever has the lesser number of passthrough revisions
	#   is better, as more can be split off, weakening the cycle
	#   more.
	# - Whichever has less links to move is better.

	set opass [$other passcount]
	if {$mycount(pass) < $opass} { return 1 } ; # self is better.
	if {$mycount(pass) > $opass} { return 0 } ; # self is worse.

	set smove [$self  linkstomove]
	set omove [$other linkstomove]

	if {$smove < $omove} { return 1 } ; # self is better.

	return 0 ; # Self is worse or equal, i.e. not better.
    }

    method break {} {
	integrity assert {[$self breakable]} {Changeset [$mycset str] is not breakable.}

	# One thing to choose when splitting CSET is where the
	# revision in categories 1 and 2 (none and passthrough
	# respectively) are moved to. This is done using the counters.

	if {!$mycount(prev)} {
	    # Nothing in category 3 => 1,2 go there, 4 the other.
	    set mycategory(prev) [concat $mycategory(none) $mycategory(pass)]
	} elseif {!$mycount(next)} {
	    # Nothing in category 4 => 1,2 go there, 3 the other.
	    set mycategory(next) [concat $mycategory(none) $mycategory(pass)]
	} elseif {$mycount(prev) < $mycount(next)} {
	    # Less predecessors than successors => 1,2 go to the
	    # sucessors.
	    set mycategory(next) [concat $mycategory(next) $mycategory(none) \
				      $mycategory(pass)]
	} else {
	    # Less successors than predecessors => 1,2 go to the
	    # predecessors.
	    set mycategory(next) [concat $mycategory(next) $mycategory(none) \
				      $mycategory(pass)]
	}

	# We now have the revisions for the two fragments to be in the
	# (prev|next) elements of mycategory.

	return [project::rev split $mycset $mycategory(prev) $mycategory(next)]
    }

    # # ## ### ##### ######## #############
    ## State

    variable myprev {} ; # Reference to predecessor changeset in the link.
    variable mycset {} ; # Reference to the main changeset of the link.
    variable mynext {} ; # Reference to the successor changeset in the link.

    # Counters for the revision categories.
    variable mycount -array {
	none 0
	prev 0
	next 0
	pass 0
    }
    # Lists of revisions for the various categories
    variable mycategory -array {
	none {}
	prev {}
	next {}
	pass {}
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc RT {r} {
	upvar 1 csetprevmap csetprevmap csetnextmap csetnextmap prevrev prevrev nextrev nextrev

	set inc	[expr {[info exists csetprevmap($r)]
		       ? [struct::set size [struct::set intersect $csetprevmap($r) $prevrev]]
		       : 0}]
	set out [expr {[info exists csetnextmap($r)]
		       ? [struct::set size [struct::set intersect $csetnextmap($r) $nextrev]]
		       : 0}]

	if {$inc && $out} { return pass }
	if {$inc}         { return prev }
	if {$out}         { return next }
	return none
    }

    proc Invert {dict} {
	array set tmp {}
	foreach {k values} $dict {
	    foreach v $values { lappend tmp($v) $k }
	}
	return [array get tmp]
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export revlink
    namespace eval revlink {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace eval project {
	    namespace import ::vc::fossil::import::cvs::project::rev
	}
	namespace import ::vc::tools::log
	log register csets
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::revlink 1.0
return
