package require Tcl 8.5
package require sqlite3
package require snit

snit::type ::vc::fossil::db {
    typemethod open_repository {{name {}}} {
    }
}
