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

## Utility package, error reporting on top of the log package.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4        ; # Required runtime.
package require vc::tools::log ; # Basic log generation.
package require snit           ; # OO system.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::tools::trouble {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    typemethod internal {text} {
	foreach line [split $text \n] { $type fatal "INTERNAL ERROR! $line" }
	exit 1
    }

    typemethod fatal {text} {
	lappend myfatal $text
	return
    }

    typemethod warn {text} {
	lappend mywarn $text
	log write 0 trouble $text
	return
    }

    typemethod info {text} {
	lappend myinfo $text
	return
    }

    typemethod show {} {
	foreach m $myinfo  { log write 0 ""      $m }
	foreach m $mywarn  { log write 0 warning $m }
	foreach m $myfatal { log write 0 fatal   $m }
	return
    }

    typemethod ? {} {
	return [expr {
	    [llength $myinfo] ||
	    [llength $mywarn] ||
	    [llength $myfatal]
	}]
    }

    typemethod abort? {} {
	if {
	    ![llength $myinfo] &&
	    ![llength $mywarn] &&
	    ![llength $myfatal]
	} return

	# Frame the pending messages to make them more clear as the
	# cause of the abort.

	set     myinfo [linsert $myinfo 0 "" "Encountered problems." ""]
	lappend myfatal "Stopped due to problems."

	# We have error messages to print, so stop now.
	exit 1
    }

    # # ## ### ##### ######## #############
    ## Internal, state

    typevariable myinfo  {}
    typevariable mywarn  {}
    typevariable myfatal {}

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

# # ## ### ##### ######## ############# #####################
## Internal. Special. Set up a hook into the application exit, to show
## the remembered messages, before passing through the regular command.

rename ::exit ::vc::tools::trouble::EXIT
proc   ::exit {{status 0}} {
    ::vc::tools::trouble show
    ::vc::tools::trouble::EXIT $status
    # Not reached.
    return
}

namespace eval ::vc::tools {
    namespace eval trouble {namespace import ::vc::tools::log }
    trouble::log register ""
    trouble::log register fatal
    trouble::log register trouble
    trouble::log register warning
    namespace export trouble
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::tools::trouble 1.0
return
