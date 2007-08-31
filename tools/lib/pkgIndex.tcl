if {![package vsatisfies [package require Tcl] 8.4]} return
package ifneeded rcsparser 1.0 [list source [file join $dir rcsparser.tcl]]
