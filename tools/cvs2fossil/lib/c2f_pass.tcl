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

## Pass manager. All passes register here, with code, description, and
## callbacks (... setup, run, finalize). Option processing and help
## query this manager to dynamically create the relevant texts.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                            ; # Required runtime.
package require snit                               ; # OO system.
package require vc::fossil::import::cvs::state     ; # State storage
package require vc::fossil::import::cvs::integrity ; # State integrity checks.
package require vc::tools::misc                    ; # Text formatting
package require vc::tools::trouble                 ; # Error reporting.
package require vc::tools::log                     ; # User feedback.
package require struct::list                       ; # Portable lassign

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::pass {
    # # ## ### ##### ######## #############
    ## Public API, Methods (Setup, query)

    typemethod define {name description command} {
	integrity assert {
	    ![info exists mydesc($name)]
	} {Multiple definitions for pass code '$name'}
	lappend mypasses $name
	set mydesc($name) $description
	set mycmd($name)  $command
	return
    }

    typemethod help {} {
	trouble info ""
	trouble info "Conversion passes:"
	trouble info ""
	set n 0

	set clen [max [struct::list map $mypasses {string length}]]
	set cfmt %-${clen}s
	set nfmt %[string length [llength $mypasses]]s

	foreach code $mypasses {
	    trouble info "  [format $nfmt $n]: [format $cfmt $code] : $mydesc($code)"
	    incr n
	}
	trouble info ""
	return
    }

    # # ## ### ##### ######## #############
    ## Public API, Methods (Execution)

    typemethod select {passdef} {
	set pl [split $passdef :]
	if {[llength $pl] > 2} {
	    trouble fatal "Bad pass definition '$passdef'"
	    trouble fatal "Expected at most one ':'"
	} elseif {[llength $pl] == 2} {
	    struct::list assign $pl start end

	    if {($start eq "") && ($end eq "")} {
		trouble fatal "Specify at least one of start- or end-pass"
		set ok 0
	    } else {
		set ok 1
		Ok? $start start ok
		Ok? $end   end   ok
	    }

	    if {$ok} {
		set mystart [Convert $start 0]
		set myend   [Convert $end   [expr {[llength $mypasses] - 1}]]
		if {$mystart > $myend} {
		    trouble fatal "Start pass is after end pass"
		}
	    }
	} elseif {[llength $pl] < 2} {
	    set start [lindex $pl 0]
	    Ok? $start "" __dummy__ 0
	    set mystart [Id $start]
	    set myend   $mystart
	}
    }

    typemethod run {} {
	if {$mystart < 0} {set mystart 0}
	if {$myend   < 0} {set myend [expr {[llength $mypasses] - 1}]}

	set skipped [lrange $mypasses 0 [expr {$mystart - 1}]]
	set run     [lrange $mypasses $mystart $myend]
	set defered [lrange $mypasses [expr {$myend + 1}] end]

	foreach p $skipped {
	    log write 0 pass "Skip  $p"
	    Call $p load
	}
	foreach p $run {
	    log write 0 pass "Setup $p"
	    Call $p setup
	}
	foreach p $run {
	    log write 0 pass "Begin $p"
	    set secbegin [clock seconds]
	    Call $p run
	    set secstop  [clock seconds]
	    log write 0 pass "Done  $p"
	    Time $p [expr {$secstop - $secbegin}]
	    trouble abort?
	}
	foreach p $defered {
	    log write 0 pass "Defer $p"
	    Call $p discard
	}

	state release
	ShowTimes
	return
    }

    typemethod current {} { return $mycurrentpass }

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Time {pass seconds} {
	::variable mytime
	lappend    mytime $pass $seconds
	ShowTime          $pass $seconds
	return
    }

    proc ShowTimes {} {
	::variable mytime
	set total 0
	foreach {pass seconds} $mytime {
	    ShowTime $pass $seconds
	    incr total $seconds
	}
	ShowTime Total $total
	return
    }

    proc ShowTime {pass seconds} {
	if {$seconds > 3600} {
	    set hr  [expr {$seconds / 3600}]
	    set min [expr {$seconds % 3600}]
	    set sec [expr {$min % 60}]
	    set min [expr {$min / 60}]

	    log write 0 pass "[format %8d $seconds] sec/$pass ([nsp $hr hour] [nsp $min minute] [nsp $sec second])"
	} elseif {$seconds > 60} {
	    set min [expr {$seconds / 60}]
	    set sec [expr {$seconds % 60}]

	    log write 0 pass "[format %8d $seconds] sec/$pass ([nsp $min minute] [nsp $sec second])"
	} else {
	    log write 0 pass "[format %8d $seconds] sec/$pass"
	}
	return
    }

    proc Ok? {code label ov {emptyok 1}} {
	upvar 1 $ov ok
	::variable mydesc
	if {$emptyok && ($code eq "")} return
	if {[info exists mydesc($code)]} return
	if {$label ne ""} {append label " "}
	trouble fatal "Bad ${label}pass code $code"
	set ok 0
	return
    }

    proc Convert {code default} {
	::variable mypasses
	return [expr {($code eq "") ? $default : [Id $code]}]
    }

    proc Id {code} {
	::variable mypasses
	return [lsearch -exact $mypasses $code]
    }

    proc Call {code args} {
	::variable mycmd
	set cmd $mycmd($code)
	foreach a $args { lappend cmd $a }
	eval $cmd
	return
    }

    # # ## ### ##### ######## #############
    ## Internal, state

    typevariable mypasses      {} ; # List of registered passes (codes).
    typevariable mydesc -array {} ; # Pass descriptions (one line).
    typevariable mycmd  -array {} ; # Pass callback command.

    typevariable mystart       -1
    typevariable myend         -1
    typevariable mytime        {} ; # Timing data for each executed pass.
    typevariable mycurrentpass {} ; # Pass currently running.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export pass
    namespace eval pass {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register pass
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::pass 1.0
return
