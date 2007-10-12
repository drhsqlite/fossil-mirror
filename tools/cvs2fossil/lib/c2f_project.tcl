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

## Project, part of a CVS repository. Multiple instances are possible.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require vc::fossil::import::cvs::file         ; # CVS archive file.
package require vc::fossil::import::cvs::state        ; # State storage
package require vc::fossil::import::cvs::project::sym ; # Per project symbols

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::project {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {path r} {
	set mybase       $path
	set myrepository $r
	return
    }

    method base {} { return $mybase }

    method printbase {} {
	if {$mybase eq ""} {return <Repository>}
	return $mybase
    }

    method add {rcs usr} {
	set myfiles($rcs) $usr
	return
    }

    method filenames {} {
	return [lsort -dict [array names myfiles]]
    }

    method files {} {
	# TODO: Loading from state
	return [TheFiles]
    }

    delegate method author   to myrepository
    delegate method cmessage to myrepository

    method getsymbol {name} {
	if {![info exists mysymbols($name)]} {
	    set mysymbols($name) [sym %AUTO% $name]
	}
	return $mysymbols($name)
    }

    # pass I persistence
    method persist {} {
	state transaction {
	    # Project data first. Required so that we have its id
	    # ready for the files.

	    state run {
		INSERT INTO project (pid,  name)
		VALUES              (NULL, $mybase);
	    }
	    set pid [state id]

	    # Then all files, with proper backreference to their
	    # project.

	    foreach {rcs usr} [array get myfiles] {
		state run {
		    INSERT INTO file (fid,  pid,  name, visible)
		    VALUES           (NULL, $pid, $rcs, $usr);
		}
	    }
	}
	return
    }

    # pass II persistence
    method persistrev {} {
	state transaction {
	    # TODO: per project persistence (symbols, meta data)
	    foreach f [TheFiles] {
		$f persist
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable mybase           {} ; # Project directory
    variable myfiles   -array {} ; # Maps rcs archive to their user files.
    variable myfobj           {} ; # File objects for the rcs archives
    variable myrepository     {} ; # Repository the prject belongs to.
    variable mysymbols -array {} ; # Map symbol names to project-level symbol objects.

    # # ## ### ##### ######## #############
    ## Internal methods

    proc TheFiles {} {
	upvar 1 myfiles myfiles myfobj myfobj self self
	if {![llength $myfobj]} {
	    set myfobj [EmptyFiles myfiles]
	}
	return $myfobj
    }

    proc EmptyFiles {fv} {
	upvar 1 $fv myfiles self self
	set res {}
	foreach f [lsort -dict [array names myfiles]] {
	    lappend res [file %AUTO% $f $self]
	}
	return $res
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export project
    namespace eval project {
	namespace import ::vc::fossil::import::cvs::file
	namespace import ::vc::fossil::import::cvs::state
	# Import not required, already a child namespace.
	# namespace import ::vc::fossil::import::cvs::project::sym
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project 1.0
return
