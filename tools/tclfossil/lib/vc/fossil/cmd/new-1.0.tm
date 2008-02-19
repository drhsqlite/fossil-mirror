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


# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::fossil::cmd 1.0                 ; # Subcommand management
package require vc::fossil::db 1.0
                
package provide vc::fossil::cmd::new 1.0
vc::fossil::cmd add new

# # ## ### ##### ######## ############# #####################
## Imports



# # ## ### ##### ######## ############# #####################
##


namespace eval ::vc::fossil::cmd {
    proc new {args} {
	if {[ui argc] != 3} {
	    ui usage "REPOSITORY-NAME"
	}
	
	set filename [file normalize [lindex [ui argv] 2]]
	db create_repository $filename
		       
    }
}