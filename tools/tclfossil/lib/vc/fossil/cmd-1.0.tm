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

## Fossil subcommand managment.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require sqlite3                             ; # Fossil database access
package require snit                                ; # OO system.


package provide vc::fossil::cmd 1.0

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::fossil {
    namespace export cmd
    snit::type cmd {
	typevariable commands ""

	typemethod add {command} {
	    lappend commands $command
	    
	}
	
	typemethod list {} {
	    return $commands
	}
    }
}

