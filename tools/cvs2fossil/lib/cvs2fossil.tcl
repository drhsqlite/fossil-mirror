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

## Main package of the cvs conversion/import facility. Loads the
## required pieces and controls their interaction.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                         ; # Required runtime.
package require snit                            ; # OO system
package require vc::fossil::import::cvs::option ; # Cmd line parsing & database
package require vc::fossil::import::cvs::pass   ; # Pass management

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    typemethod run {arguments} {
	# Run a series of passes over the cvs repository to extract,
	# filter, and order its historical information. Which passes
	# are actually run is determined through the specified options
	# and their defaults.

	option process $arguments
	pass run
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs 1.0
return
