## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Mark Janssen.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Command line user interface for tclfossil.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::fossil::cmd::clone 1.0          ; # Clone command

package provide vc::fossil::ui 1.0

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::fossil {
    snit::type ui {
	typevariable argv
	typevariable argc
	typevariable command
	typevariable fSqlTrace
	typevariable fUser

	typemethod run {args} {
	    
	    # TODO parse options
	    set argv $args
	    set argc [llength $args]

	    if {$argc < 2} {
		ui usage "COMMAND ..."
	    }

	    # TODO better command searching so prefixes work
	    set command [lindex $argv 1]
	    set commands [vc::fossil::cmd list]
	       
	    if {[lsearch $commands $command] < 0} {
		puts "unknown command: $command"
		puts {use "help" for more information}
		exit 1
	    }
	    vc::fossil::cmd::$command {*}[lrange $argv 1 end]
	    return
	}
	
	typemethod usage {str} {
	    puts stderr "Usage: [lrange $argv 0 1] $str"
	    exit 1
	}
	
	typemethod panic {str} {
	    puts stderr "[lindex $argv 0]: $str"
	    exit 1
	}
	    
	
	typemethod argc {} {
	    return $argc
	}

	typemethod argv {} {
	    return $argv
	}
    }
}