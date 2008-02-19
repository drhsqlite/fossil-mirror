package require Tcl 8.5
package require sqlite3
package require snit

snit::type ::vc::fossil::db {
    variable db
    method open_repository {{name {}}} {
	sqlite3 db1 c:/src/fossil.fsl
        set db db1
    }
    method revlist {} {
	$db eval {select uuid from blob}
    }
}

vc::fossil::db create fossildb

fossildb open_repository
puts [fossildb revlist]
