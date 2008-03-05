## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007-2008 Andreas Kupries.
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

package require Tcl 8.4                                 ; # Required runtime.
package require snit                                    ; # OO system.
package require vc::fossil::import::cvs::file           ; # CVS archive file.
package require vc::fossil::import::cvs::state          ; # State storage.
package require vc::fossil::import::cvs::project::rev   ; # Changesets.
package require vc::fossil::import::cvs::project::sym   ; # Per project symbols.
package require vc::fossil::import::cvs::project::trunk ; # Per project trunk, main lod
package require vc::tools::log                          ; # User feedback
package require struct::list                            ; # Advanced list operations..

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::project {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {path r} {
	set mybase       $path
	set myrepository $r
	set mytrunk      [trunk %AUTO% $self]
	set mysymbol([$mytrunk name]) $mytrunk
	return
    }

    method base  {} { return $mybase  }
    method trunk {} { return $mytrunk }

    method fullpath {} { return [$myrepository base?]/$mybase }

    method printbase {} {
	if {$mybase eq ""} {return <Repository>}
	return $mybase
    }

    method id    {}   { return $myid }
    method setid {id} { set myid $id ; return }

    method addfile {rcs usr executable {fid {}}} {
	set myfiles($rcs) [list $usr $executable $fid]
	return
    }

    method filenames {} {
	return [lsort -dict [array names myfiles]]
    }

    method files {} {
	return [TheFiles]
    }

    delegate method defauthor       to myrepository
    delegate method defcmessage     to myrepository
    delegate method trunkonly       to myrepository
    delegate method commitmessageof to myrepository

    method defmeta {bid aid cid} {
	return [$myrepository defmeta $myid $bid $aid $cid]
    }

    method getsymbol {name} {
	if {![info exists mysymbol($name)]} {
	    set mysymbol($name) \
		[sym %AUTO% $name [$myrepository defsymbol $myid $name] $self]
	}
	return $mysymbol($name)
    }

    method hassymbol {name} {
	return [info exists mysymbol($name)]
    }

    method purgeghostsymbols {} {
	set changes 1
	while {$changes} {
	    set changes 0
	    foreach {name symbol} [array get mysymbol] {
		if {![$symbol isghost]} continue
		log write 3 project "$mybase: Deleting ghost symbol '$name'"
		$symbol destroy
		unset mysymbol($name)
		set changes 1
	    }
	}
	return
    }

    method determinesymboltypes {} {
	foreach {name symbol} [array get mysymbol] {
	    $symbol determinetype
	}
	return
    }

    # pass I persistence
    method persist {} {
	TheFiles ; # Force id assignment.

	state transaction {
	    # Project data first. Required so that we have its id
	    # ready for the files.

	    state run {
		INSERT INTO project (pid,  name)
		VALUES              (NULL, $mybase);
	    }
	    set myid [state id]

	    # Then all files, with proper backreference to their
	    # project.

	    foreach rcs [lsort -dict [array names myfiles]] {
		struct::list assign $myfiles($rcs) usr executable _fid_
		state run {
		    INSERT INTO file (fid,  pid,   name, visible, exec)
		    VALUES           (NULL, $myid, $rcs, $usr,    $executable);
		}
		$myfmap($rcs) setid [state id]
	    }
	}
	return
    }

    # pass II persistence
    method persistrev {} {
	# Note: The per file information (incl. revisions and symbols)
	# has already been saved and dropped. This was done
	# immediately after processing it, i.e. as part of the main
	# segment of the pass, to keep out use of memory under
	# control.
	#
	# The repository level information has been saved as well too,
	# just before saving the projects started. So now we just have
	# to save the remaining project level parts to fix the
	# left-over dangling references, which are the symbols.

	state transaction {
	    foreach {name symbol} [array get mysymbol] {
		$symbol persistrev
	    }
	}
	return
    }

    method changesetsinorder {} {
	return [rev inorder $myid]
    }

    delegate method getmeta to myrepository

    # # ## ### ##### ######## #############
    ## State

    variable mybase           {} ; # Project directory.
    variable myid             {} ; # Project id in the persistent state.
    variable mytrunk          {} ; # Reference to the main line of
				   # development for the project.
    variable myfiles   -array {} ; # Maps the rcs archive paths to
				   # their user-visible files.
    variable myfobj           {} ; # File objects for the rcs archives
    variable myfmap    -array {} ; # Map rcs archive to their object.
    variable myrepository     {} ; # Repository the project belongs to.
    variable mysymbol  -array {} ; # Map symbol names to project-level
				   # symbol objects.

    # # ## ### ##### ######## #############
    ## Internal methods

    proc TheFiles {} {
	upvar 1 myfiles myfiles myfobj myfobj self self myfmap myfmap
	if {![llength $myfobj]} {
	    set myfobj [EmptyFiles myfiles]
	}
	return $myfobj
    }

    proc EmptyFiles {fv} {
	upvar 1 $fv myfiles self self myfmap myfmap
	set res {}
	foreach rcs [lsort -dict [array names myfiles]] {
	    struct::list assign $myfiles($rcs) f executable fid
	    set file [file %AUTO% $fid $rcs $f $executable $self]
	    lappend res $file
	    set myfmap($rcs) $file
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
	namespace import ::vc::tools::log
	namespace import ::vc::fossil::import::cvs::file
	namespace import ::vc::fossil::import::cvs::state
	# Import not required, already a child namespace.
	# namespace import ::vc::fossil::import::cvs::project::sym
	# Import not required, already a child namespace.
	# namespace import ::vc::fossil::import::cvs::project::rev
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project 1.0
return
