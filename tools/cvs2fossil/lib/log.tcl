## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007-2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Utility package, basic user feedback

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4        ; # Required runtime
package require snit           ; # OO system.
package require vc::tools::mem ; # Memory tracking.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::tools::log {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    # Write the message 'text' to log, for the named 'system'. The
    # message is written if and only if the message verbosity is less
    # or equal the chosen verbosity. A message of verbosity 0 cannot
    # be blocked.

    typemethod write {verbosity system text} {
	if {$verbosity > $myloglevel} return
	uplevel #0 [linsert $mylogcmd end write [System $system] \
	    [uplevel 1 [list ::subst $text]]]
	return
    }

    # Similar to write, especially in the handling of the verbosity,
    # to drive progress displays. It signals that for some long
    # running operation we are at tick 'n' of at most 'max' ticks. An
    # empty 'max' indicates an infinite progress display.

    typemethod progress {verbosity system n max} {
	if {!$myprogress}             return
	if {$verbosity > $myloglevel} return
	uplevel #0 [linsert $mylogcmd end progress [System $system] $n $max]
	return
    }

    typemethod visible? {verbosity} {
	return [expr {$verbosity <= $myloglevel}]
    }

    # # ## ### ##### ######## #############
    # Public API, Administrative methods

    # Set verbosity to the chosen 'level'. Only messages with a level
    # less or equal to this one will be shown.

    typemethod verbosity {level} {
	if {$level < 1} {set level 0}
	set myloglevel $level
	return
    }

    typemethod verbose {} {
	incr myloglevel
	return
    }

    typemethod noprogress {} {
	set myprogress 0
	return
    }

    typemethod quiet {} {
	if {$myloglevel < 1} return
	incr myloglevel -1
	return
    }

    # Query the currently set verbosity.

    typemethod verbosity? {} {
	return  $myloglevel
    }

    # Set the log callback handling the actual output of messages going
    # through the package.

    typemethod command {cmdprefix} {
	variable mylogcmd $cmdprefix
	return
    }

    # Register a system name, to enable tabular formatting. This is
    # done by setting up a format specifier with a proper width. This
    # is handled in the generation command, before the output callback
    # is invoked.

    typemethod register {name} {
	set nlen [string length $name]
	if {$nlen < $mysyslen} return
	set mysyslen $nlen
	set mysysfmt %-${mysyslen}s
	return
    }

    # # ## ### ##### ######## #############
    ## Internal, state

    typevariable myloglevel 2                     ; # Some verbosity, not too much
    typevariable mylogcmd   ::vc::tools::log::OUT ; # Standard output to stdout.
    typevariable mysysfmt   %s                    ; # Non-tabular formatting.
    typevariable mysyslen   0                     ; # Ditto.
    typevariable myprogress 1                     ; # Progress output is standard.

    # # ## ### ##### ######## #############
    ## Internal, helper methods (formatting, dispatch)

    proc System {s} {
	::variable mysysfmt
	return [format $mysysfmt $s]
    }

    # # ## ### ##### ######## #############
    ## Standard output callback, module internal

    # Dispatch to the handlers of the possible operations.

    proc OUT {op args} {
	eval [linsert $args 0 ::vc::tools::log::OUT/$op]
	return
    }

    # Write handler. Each message is a line.

    proc OUT/write {system text} {
	set m [mlog]
	regsub -all {[^	]} $m { } b
	puts "$m$system [join [split $text \n] "\n$b$system "]"
	mlimit
	return
    }

    # Progress handler. Uses \r to return to the beginning of the
    # current line without advancing.

    proc OUT/progress {system n max} {
	if {$max eq {}} {
	    puts -nonewline "$system $n\r"
	} else {
	    puts -nonewline "$system [format %[string length $max]s $n]/$max\r"
	}
	flush stdout
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools {
    namespace export log
    namespace eval log {
	namespace import ::vc::tools::mem::mlog
	namespace import ::vc::tools::mem::mlimit
    }
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::log 1.0
return
