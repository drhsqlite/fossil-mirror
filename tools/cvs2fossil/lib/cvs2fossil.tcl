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

# # ## ### ##### ######## ############# #####################
## Passes. The order in which the various passes are loaded is
##         important. It is the same order in which they will
##         register, and then be run in.

package require vc::fossil::import::cvs::pass::collar      ; # Coll'ect Ar'chives.
package require vc::fossil::import::cvs::pass::collrev     ; # Coll'ect Rev'isions.
package require vc::fossil::import::cvs::pass::collsym     ; # Coll'ate Sym'bols
package require vc::fossil::import::cvs::pass::filtersym   ; # Filter'  Sym'bols

# Note: cvs2svn's SortRevisionSummaryPass and SortSymbolSummaryPass
#       are not implemented by us. They are irrelevant due to our use
#       of a relational database proper for the persistent state,
#       allowing us to sort the data on the fly as we need it.

package require vc::fossil::import::cvs::pass::initcsets   ; # Init'ialize C'hange'Sets
package require vc::fossil::import::cvs::pass::csetdeps    ; # C'hange'Set Dep'endencies
package require vc::fossil::import::cvs::pass::breakrcycle ; # Break' R'evision Cycle's
package require vc::fossil::import::cvs::pass::rtopsort    ; # R'evision Top'ological Sort'
package require vc::fossil::import::cvs::pass::breakscycle ; # Break' S'ymbol Cycle's
package require vc::fossil::import::cvs::pass::breakacycle ; # Break' A'll Cycle's
package require vc::fossil::import::cvs::pass::atopsort    ; # A'll Top'ological Sort'
package require vc::fossil::import::cvs::pass::importfiles ; # Import' Files
package require vc::fossil::import::cvs::pass::importcsets ; # Import' Changesets
package require vc::fossil::import::cvs::pass::importfinal ; # Import' Finalization

# # ## ### ##### ######## ############# #####################
## Support for passes etc.

package require vc::fossil::import::cvs::option ; # Cmd line parsing & database
package require vc::fossil::import::cvs::pass   ; # Pass management
package require vc::tools::log                  ; # User feedback

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

	vc::tools::log write 0 cvs2fossil Done
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
