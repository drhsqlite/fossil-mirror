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

## State manager. Maintains the sqlite database used by all the other
## parts of the system, especially the passes and their support code,
## to persist and restore their state across invokations.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                          ; # Required runtime.
package require snit                             ; # OO system.
package require fileutil                         ; # File operations.
package require sqlite3                          ; # Database access.
package require vc::tools::trouble               ; # Error reporting.
package require vc::tools::log                   ; # User feedback.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::state {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod usedb {path} {
	# Immediate validation. There are are two possibilities to
	# consider. The path exists or it doesn't.

	# In the first case it has to be a readable and writable file,
	# and it has to be a proper sqlite database. Further checks
	# regarding the required tables will be done later, by the
	# passes, during their setup.

	# In the second case we have to be able to create the file,
	# and check that. This is done by opening it, sqlite will then
	# try to create it, and may fail.

	if {[::file exists $path]} {
	    if {![fileutil::test $path frw msg {cvs2fossil state}]} {
		trouble fatal $msg
		return
	    }
	}

	if {[catch {
	    sqlite3 ${type}::TEMP $path
	    ${type}::TEMP eval {PRAGMA synchronous=OFF;}
	} res]} {
	    trouble fatal $res
	    return
	}

	# A previously defined state database is closed before
	# committing to the new definition. We do not store the path
	# itself, this ensures that the file is _not_ cleaned up after
	# a run.

	set mystate ${type}::STATE
	set mypath  {}

	catch { $mystate close }
	rename  ${type}::TEMP $mystate

	log write 2 state "is $path"
	return
    }

    typemethod setup {} {
	# If, and only if no state database was defined by the user
	# then it is now the time to create our own using a tempfile.

	if {$mystate ne ""} return

	set mypath  [fileutil::tempfile cvs2fossil_state_]
	set mystate ${type}::STATE
	sqlite3 $mystate $mypath
        $mystate eval {PRAGMA synchronous=OFF;}

	log write 2 state "using $mypath"
	return
    }

    typemethod release {} {
	log write 2 state release
	${type}::STATE close
	if {$mypath eq ""} return
	::file delete $mypath
	return
    }

    # Declare a table needed for the storing of persistent state, and
    # its structure. A possibly previously existing definition is
    # dropped. To be used when a table is needed and not assumed to
    # exist from previous passes.

    typemethod extend {name definition {indices {}}} {
	log write 5 state "extend $name"
	Save "extend $name ================================"

	$mystate transaction {
	    catch { $mystate eval "DROP TABLE $name" }
	    $mystate eval "CREATE TABLE $name ( $definition )"

	    set id 0
	    foreach columns $indices {
		log write 5 state "index  $name$id"

		$mystate eval "CREATE INDEX ${name}$id ON ${name} ( [join $columns ,] )"
		incr id
	    }
	}
	return
    }

    # Declare that a table is needed for reading from and/or storing
    # to persistent state, and is assumed to already exist. A missing
    # table is an internal error causing an immediate exit.

    typemethod use {name} {
	log write 5 state "use    $name"
	Save "use $name ==================================="

	set found [llength [$mystate eval {
	    SELECT name
	    FROM sqlite_master
	    WHERE type = 'table'
	    AND   name = $name
	    ;
	}]]

	# No assert, would cause cycle in package dependencies
	if {$found} return
	trouble internal "The required table \"$name\" is not defined."
	# Not reached
	return
    }

    typemethod discard {name} {
	# Method for a user to remove outdated information from the
	# persistent state, table by table.

	log write 5 state "discard $name"

	$mystate transaction {
	    catch { $mystate eval "DROP TABLE $name" }
	}
	return
    }

    typemethod run {args} {
	Save $args
	return [uplevel 1 [linsert $args 0 $mystate eval]]
    }

    typemethod foreachrow {sql script} {
	Save $sql
	uplevel 1 [list $mystate eval $sql $script]
	return
    }

    typemethod one {args} {
	Save $args
	return [uplevel 1 [linsert $args 0 $mystate onecolumn]]
    }

    typemethod transaction {script} {
	return [uplevel 1 [list $mystate transaction $script]]
    }

    typemethod id {} {
	return [$mystate last_insert_rowid]
    }

    typemethod savequeriesto {path} {
	set mysavepath $path
	return
    }

    # # ## ### ##### ######## #############

    proc Save {text} {
	::variable mysavepath
	if {$mysavepath eq ""} return
	fileutil::appendToFile $mysavepath $text\n\n
	return
    }

    # # ## ### ##### ######## #############
    ## State

    typevariable mystate    {} ; # Sqlite database (command) holding the converter state.
    typevariable mypath     {} ; # Path to the database, for cleanup of a temp database.
    typevariable mysavepath {} ; # Path where to save queries for introspection.

    # # ## ### ##### ######## #############
    ## Internal methods


    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export state
    namespace eval state {
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register state
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::state 1.0
return
