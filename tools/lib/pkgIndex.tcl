if {![package vsatisfies [package require Tcl] 8.4]} return
package ifneeded rcsparser 1.0 [list source [file join $dir rcsparser.tcl]]
package ifneeded vc::cvs::ws             1.0 [list source [file join $dir cvs.tcl]]
package ifneeded vc::fossil::ws          1.0 [list source [file join $dir fossil.tcl]]
package ifneeded vc::fossil::import::cvs 1.0 [list source [file join $dir importcvs.tcl]]
package ifneeded vc::tools::log          1.0 [list source [file join $dir log.tcl]]
