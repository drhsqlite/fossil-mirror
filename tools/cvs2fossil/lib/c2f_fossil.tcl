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
	return
    }

    method initialize {} {
	set myrepository [fileutil::tempfile cvs2fossil_repo_]
	set myworkspace  [fileutil::tempfile cvs2fossil_wspc_]
	::file delete $myworkspace
	::file mkdir  $myworkspace

	Do new [::file nativename $myrepository]
	$self InWorkspace ; Do open [::file nativename $myrepository]
	$self RestorePwd

	log write 8 fossil {Scratch repository created @ $myrepository}
	log write 8 fossil {Scratch workspace  created @ $myworkspace }
	return
    }

    method load {r w} {
	set myrepository $r
	set myworkspace  $w

	log write 8 fossil {Scratch repository found @ $myrepository}
	log write 8 fossil {Scratch workspace  found @ $myworkspace}
	return
    }

    method space {} {
	return [list $myrepository $myworkspace]
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
	# Massage the commit message to remember the old user name
	# which did the commit in CVS.

	set message "By $user:\n$message"

	log write 2 fossil {== $user @ [clock format $date]}
	log write 2 fossil {-> $parent}
	log write 9 fossil {%% [join [split $message \n] "\n%% "]}

	lappend cmd Do test-import-manifest $date $message
	if {$parent ne ""} { lappend cmd -p $parent }
	foreach {frid fpath flabel} $revisions {
	    lappend cmd -f $frid $fpath
	    log write 12 fossil {** <[format %5d $frid]> = <$flabel>}
	}

	# run fossil test-command performing the import.
	log write 12 fossil {	[lreplace $cmd 3 3 @@]}

	$self InWorkspace
	set res [eval $cmd]
	$self RestorePwd

	integrity assert {
	    [regexp {^inserted as record \d+, [0-9a-fA-F]+$} $res]
	} {Unable to process unexpected fossil output '$res'}
	set rid  [string trim [lindex $res 3] ,]
	set uuid [lindex $res 4]

	log write 2 fossil {== $rid ($uuid)}

	return [list $rid $uuid]
    }

    method tag {uuid name} {
	log write 2 fossil {Tag '$name' @ $uuid}

	$self InWorkspace
	Do tag add sym-$name $uuid
	$self RestorePwd
	return
    }

    method branchmark {uuid name} {
	# We do not mark the trunk
	if {$name eq ":trunk:"} return

	log write 2 fossil {Begin branch '$name' @ $uuid}

	$self InWorkspace
	Do tag branch sym-$name $uuid
	$self RestorePwd
	return
    }

    method branchcancel {uuid name} {
	# The trunk is unmarked, thus cancellation is not needed
	# either.
	if {$name eq ":trunk:"} return

	log write 2 fossil {Cancel branch '$name' @ $uuid}

	$self InWorkspace
	Do tag delete sym-$name $uuid
	$self RestorePwd
	return
    }

    method finalize {destination} {
	log write 2 fossil {Finalize, rebuilding repository}
	Do rebuild [::file nativename $myrepository]

	::file rename -force $myrepository $destination
	::file delete -force $myworkspace
	$self destroy

	log write 2 fossil {destination $destination}
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
	log write 14 fossil {Doing '$args'}
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
