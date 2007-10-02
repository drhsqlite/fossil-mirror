# # ## ### ##### ######## ############# #####################
## Package management.
## Index of the local packages required by cvs2fossil
# # ## ### ##### ######## ############# #####################
if {![package vsatisfies [package require Tcl] 8.4]} return
package ifneeded vc::fossil::import::cvs         1.0 [list source [file join $dir cvs2fossil.tcl]]
package ifneeded vc::fossil::import::cvs::option 1.0 [list source [file join $dir c2f_option.tcl]]
