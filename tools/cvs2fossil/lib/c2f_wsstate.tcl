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

## Track the state of a cvs workspace as changesets are committed to
## it. Nothing actually happens in the filesystem, this is completely
## virtual.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require struct::list                        ; # List assignment
package require vc::tools::log                      ; # User feedback.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::wsstate {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {lod} {
	# Start with an empty state
	set myname   $lod
	set myticks  0
	set myparent {}
	return
    }

    method name   {} { return $myname }
    method ticks  {} { return $myticks }

    method add {oprevisioninfo} {
	# oprevisioninfo = list (rid path label op ...) /quadruples

	# Overwrite all changed files (identified by path) with the
	# new revisions. This keeps all unchanged files. Files marked
	# as dead are removed.

	foreach {rid path label rop} $oprevisioninfo {
	    log write 5 wss {$myop($rop) $label}

	    if {$rop < 0} {
		if {[catch {
		    unset mystate($path)
		}]} {
		    log write 0 wss "Removed path \"$path\" is not known to the workspace"
		}
	    } else {
		set mystate($path) [list $rid $label]
	    }
	}

	incr myticks
	return
    }

    method get {} {
	set res {}
	foreach path [lsort -dict [array names mystate]] {
	    struct::list assign $mystate($path) rid label
	    lappend res $rid $path $label
	}
	return $res
    }

    method defid {id} {
	set myid $id
	return
    }

    method getid {} { return $myid }

    method defstate {s} { array set mystate $s ; return }
    method getstate {}  { return [array get mystate] }

    method parent {} { return $myparent }
    method defparent {parent} {
	set myparent $parent
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myname {}         ; # Name of the LOD the workspace is
				 # for.
    variable myid   {}         ; # Record id of the fossil manifest
				 # associated with the current state.
    variable mystate -array {} ; # Map from paths to the recordid of
				 # the file revision behind it, and
				 # the associated label for logging.
    variable myticks 0         ; # Number of 'add' operations
				 # performed on the state.
    variable myparent {}       ; # Reference to the parent workspace.

    typevariable myop -array {
	-1 REM
	0  ---
	1  ADD
	2  CHG
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export wsstate
    namespace eval wsstate {
	namespace import ::vc::tools::log
	log register wss
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::wsstate 1.0
return
