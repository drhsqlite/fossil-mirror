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

## Fossil, a helper class managing the access to fossil repositories.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require fileutil                            ; # Temp.dir/file
package require snit                                ; # OO system.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::log                      ; # User feedback
package require vc::fossil::import::cvs::integrity  ; # State integrity checks.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::fossil {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {} {
	set myrepository [fileutil::tempfile cvs2fossil_repo_]
	set myworkspace  [fileutil::tempfile cvs2fossil_wspc_]
	::file delete $myworkspace
	::file mkdir  $myworkspace

	Do new [::file nativename $myrepository]
	$self InWorkspace ; Do open [::file nativename $myrepository]
	$self RestorePwd
	return
    }

    # # ## ### ##### ######## #############
    ##

    method root {} {
	# The id of the root manifest is hardwired into fossil. This
	# manifest is created when a new repository is made (See
	# 'new', in the constructor).
	return 1
    }

    method workspace {} { return $myworkspace }

    method importfiles {map} {
	# map = list (instruction), instruction = add|delta
	# add   = list ('A', path)
	# delta = list ('D', path, src)

	log write 3 fossil {Importing revisions...}

	array set id {}
	$self InWorkspace

	set n   0
	set max [llength $map]

	foreach insn $map {
	    log progress 3 fossil $n $max ; incr n

	    struct::list assign $insn cmd pa pb
	    switch -exact -- $cmd {
		A {
		    log write 8 fossil {Importing   <$pa>,}

		    # Result = 'inserted as record :FOO:'
		    #           0        1  2     3
		    set res [Do test-content-put $pa]
		    integrity assert {
			[regexp {^inserted as record \d+$} $res]
		    } {Unable to process unexpected fossil output '$res'}
		    set id($pa) [lindex $res 3]
		}
		D {
		    log write 8 fossil {Compressing <$pa>, as delta of <$pb>}

		    Do test-content-deltify $id($pa) $id($pb) 1
		}
	    }
	}
	$self RestorePwd

	log write 3 fossil Done.
	return [array get id]
    }

    method importrevision {label user message date parent revisions} {
	# TODO = Write the actual import, and up the log level.

	log write 2 fossil {== $user @ [clock format $date]}
	log write 2 fossil {-> $parent}
	log write 2 fossil {%% [join [split $message \n] "\n%% "]}

	set uuids {}
	foreach {uuid fname revnr} $revisions {
	    lappend uuids $uuid
	    log write 2 fossil {** $fname/$revnr = <$uuid>}
	}

	# Massage the commit message to remember the old user name
	# which did the commit in CVS.

	set message "By $user:\n$message"

	# run fossil test-command performing the import.
	#

	log write 2 fossil {== $label}
	return $label ; # FAKE a uuid for the moment
    }

    method finalize {destination} {
	::file rename -force $myrepository $destination
	::file delete -force $myworkspace
	$self destroy
	return
    }

    # # ## ### ##### ######## #############
    ##

    typemethod setlocation {path} {
	set myfossilcmd    $path
	set myneedlocation 0
	return
    }

    typemethod validate {} {
	if {!$myneedlocation} {
	    if {![fileutil::test $myfossilcmd efrx msg]} {
		trouble fatal "Bad path for fossil executable: $msg"
	    }
	} else {
	    trouble fatal "Don't know where to find the 'fossil' executable"
	}
	return
    }

    typeconstructor {
	set location [auto_execok fossil]
	set myneedlocation [expr {$location eq ""}]
	if {$myneedlocation} return
	$type setlocation $location
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable mypwd        {} ; # Path to last CWD
    variable myrepository {} ; # Path to our fossil database.
    variable myworkspace  {} ; # Path to the workspace for our fossil
			       # database.

    typevariable myfossilcmd    ; # Path to fossil executable.
    typevariable myneedlocation ; # Boolean, indicates if user has to
				  # tell us where fossil lives or not.

    # # ## ### ##### ######## #############
    ## Internal methods

    proc Do {args} {
	# 8.5: exec $myfossilcmd {*}$args
	return [eval [linsert $args 0 exec $myfossilcmd]]
    }

    method InWorkspace {} { set mypwd [pwd] ; cd $myworkspace ; return }
    method RestorePwd  {} { cd $mypwd       ; set mypwd {}    ; return }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export fossil
    namespace eval   fossil {
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	namespace import ::vc::fossil::import::cvs::integrity
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::fossil 1.0
return
