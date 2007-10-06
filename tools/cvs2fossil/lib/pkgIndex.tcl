# # ## ### ##### ######## ############# #####################
## Package management.
## Index of the local packages required by cvs2fossil
# # ## ### ##### ######## ############# #####################
if {![package vsatisfies [package require Tcl] 8.4]} return
package ifneeded vc::fossil::import::cvs                1.0 [list source [file join $dir cvs2fossil.tcl]]
package ifneeded vc::fossil::import::cvs::option        1.0 [list source [file join $dir c2f_option.tcl]]
package ifneeded vc::fossil::import::cvs::pass          1.0 [list source [file join $dir c2f_pass.tcl]]
package ifneeded vc::fossil::import::cvs::pass::collar  1.0 [list source [file join $dir c2f_pcollar.tcl]]
package ifneeded vc::fossil::import::cvs::pass::collrev 1.0 [list source [file join $dir c2f_pcollrev.tcl]]
package ifneeded vc::fossil::import::cvs::repository    1.0 [list source [file join $dir c2f_repository.tcl]]
package ifneeded vc::fossil::import::cvs::project       1.0 [list source [file join $dir c2f_project.tcl]]
package ifneeded vc::fossil::import::cvs::state         1.0 [list source [file join $dir c2f_state.tcl]]
package ifneeded vc::tools::trouble                     1.0 [list source [file join $dir trouble.tcl]]
package ifneeded vc::tools::log                         1.0 [list source [file join $dir log.tcl]]
package ifneeded vc::tools::misc                        1.0 [list source [file join $dir misc.tcl]]

