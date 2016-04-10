## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Utilities for memory tracking

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4      ; # Required runtime
package require struct::list ; # List assignment

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::tools::mem {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    if {[llength [info commands memory]]} {
	proc minfo {} {
	    # memory info reduced to the set of relevant numbers in the output
	    struct::list assign [split [memory info] \n] tm tf cpa cba mpa mba
	    struct::list assign $tm  _ _   tm
	    struct::list assign $tf  _ _   tf
	    struct::list assign $cpa _ _ _ cpa
	    struct::list assign $cba _ _ _ cba
	    struct::list assign $mpa _ _ _ mpa
	    struct::list assign $mba _ _ _ mba
	    return [list $tm $tf $cpa $cba $mpa $mba]
	}
    } else {
	proc minfo {} {return {0 0 0 0 0 0}}
    }

    proc mlog {} {
	variable track ; if {!$track} { return {} }

	variable lcba
	variable lmba
	variable mid

	struct::list assign [minfo] _ _ _ cba _ mba

	set dc [expr $cba - $lcba] ; set lcba $cba
	set dm [expr $mba - $lmba] ; set lmba $mba

	# projection: 1          2 3          4 5         6 7          6 8         10
	return "[F [incr mid]] | [F $cba] | [F $dc] | [F $mba] | [F $dm] |=| "
    }

    proc mark {} {
	variable track ; if {!$track} return
	variable mid
	variable lcba
	variable lmark
	set dm [expr {$lcba - $lmark}]
	puts  "[F $mid] | [F $lcba] | [F $dm] | [X %] | [X %] |@| [X %]"
	set lmark $lcba
	return
    }

    proc F {n} { format %10d $n }
    proc X {c} { string repeat $c 10 }

    proc mlimit {} {
	variable track ; if {!$track} return ; # No checks if there is no memory tracking
	variable limit ; if {!$limit} return ; # No checks if there is no memory limit set

	struct::list assign [minfo] _ _ _ cba _ _

	# Nothing to do if we are still under the limit
	if {$cba <= $limit} return

	# Notify user and kill the importer
	puts ""
	puts "\tMemory limit breached: $cba > $limit"
	puts ""
	exit 1
    }

    proc setlimit {thelimit} {
	# Activate memory tracking, and set the limit. The specified
	# limit is taken relative to the amount of memory allocated at
	# the time of the call.

	variable limit
	struct::list assign [minfo] _ _ _ cba _ _
	set limit [expr $cba + $thelimit]

	track
	return
    }

    proc notrack {} {
	variable track 0
	return
    }

    proc track {} {
	variable track 1
	return
    }

    # # ## ### ##### ######## #############

    variable track 0 ; # Boolean flag. If set this module will track
		       # memory, inserting the relevant information
		       # whenever the application logs something.
    variable limit 0 ; # The maximum amount of memory allowed to the
		       # application. This module will abort when
		       # 'current bytes allocated' goes over this
		       # value.

    variable lcba 0 ; # Last 'current bytes allocated' (cba)
    variable lmba 0 ; # Last 'maximum bytes allocated' (mba)
    variable mid  0 ; # Memory id, abstract time
    variable lmark 0 ; #

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools::mem {
    namespace export minfo mlog track notrack mlimit setlimit mark
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::mem 1.0
return
