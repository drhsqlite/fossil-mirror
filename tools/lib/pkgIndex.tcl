if {![package vsatisfies [package require Tcl] 8.4]} return
package ifneeded rcsparser 1.0 [list source [file join $dir rcsparser.tcl]]
package ifneeded cvs       1.0 [list source [file join $dir cvs.tcl]]
package ifneeded fossil    1.0 [list source [file join $dir fossil.tcl]]
