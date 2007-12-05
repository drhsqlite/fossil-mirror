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

# A tool package, provides a parser for RCS archive files. This parser
# is implemented via recursive descent. It is not only given a file to
# process, but also a 'sink', an object it calls out to at important
# places of the parsing process to either signal an event and/or
# convey gathered information to it. The sink is responsible for the
# actual processing of the data in whatever way it desires.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require fileutil                            ; # File utilities.
package require vc::tools::log                      ; # User feedback.
package require struct::list                        ; # Advanced list ops.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::rcs::parser {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod process {path sink} {
	Initialize $path $sink
	Call begin
	Admin ; Deltas ; Description ; DeltaTexts
	Call done
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods, recursive descent, syntactical processing

    proc Admin {} {
	Head ; PrincipalBranch ; Access ; Symbols
	Locks ; Strictness ; FileComment ; Expand
	Call admindone
	return
    }

    # # ## ### ##### ######## #############

    proc Head {} {
	RequiredLiteral head
	RequiredNumber -> head
	Semicolon
	Call sethead $head
	return
    }

    proc PrincipalBranch {} {
	if {![OptionalLiteral branch]} return
	RequiredNumber -> branch
	Semicolon
	Call setprincipalbranch $branch
	return
    }

    proc Access {} {
	RequiredLiteral access ;
	Semicolon
	return
    }

    proc Symbols {} {
	RequiredLiteral symbols
	while {[Ident -> symbol]} {
	    if {
		![regexp {^\d*[^,.:;@$]([^,.:;@$]*\d*)*$} $symbol] ||
		[string match */ $symbol]
	    } {
		Rewind
		Bad {symbol name}
	    }
	    RequiredNumber -> rev
	    Call deftag $symbol $rev
	}
	Semicolon
	return
    }

    proc Locks {} {
	# Not saving locks.
	RequiredLiteral locks
	while {[Ident -> symbol]} {
	    RequiredNumber -> l
	}
	Semicolon
	return
    }

    proc Strictness {} {
	# Not saving strictness
	if {![OptionalLiteral strict]} return
	Semicolon
	return
    }

    proc FileComment {} {
	if {![OptionalLiteral comment]} return
	if {![OptionalString -> c]} return
	Semicolon
	Call setcomment $c
	return
    }

    proc Expand {} {
	# Not saving expanded keywords
	if {![OptionalLiteral expand]} return
	if {![OptionalString -> dummy]} return
	Semicolon
	return
    }

    # # ## ### ##### ######## #############

    proc Deltas {} {
	set ok [OptionalNumber -> rev]
	while {$ok} {
	    Date     -> d
	    Author   -> a
	    State    -> s
	    Branches -> b
	    NextRev  -> n
	    Call def $rev $d $a $s $n $b

	    # Check if this is followed by a revision number or the
	    # literal 'desc'. If neither we consume whatever is there
	    # until the next semicolon, as it has to be a 'new
	    # phrase'. Otherwise, for a revision number we loop back
	    # and consume that revision, and lastly for 'desc' we stop
	    # completely as this signals the end of the revision tree
	    # and the beginning of the deltas.

	    while {1} {
		set ok [OptionalNumber -> rev]
		if {$ok} break

		if {[LiteralPeek desc]} {
		    set ok 0
		    break
		}

		Anything -> dummy
		Semicolon
	    }
	}
	Call defdone
	return
    }

    # # ## ### ##### ######## #############

    proc Date {_ dv} {
	upvar 1 $dv d
	RequiredLiteral date
	RequiredNumber -> d
	Semicolon

	struct::list assign [split $d .] year month day hour min sec
	if {$year < 100} {incr year 1900}
	set d [clock scan "${year}-${month}-${day} ${hour}:${min}:${sec}"]
	return
    }

    proc Author {_ av} {
	upvar 1 $av a
	RequiredLiteral author
	Anything -> a
	Semicolon
	return
    }

    proc State {_ sv} {
	upvar 1 $sv s
	RequiredLiteral state
	Anything -> s
	Semicolon
	return
    }

    proc Branches {_ bv} {
	upvar 1 $bv b
	RequiredLiteral branches
	Anything -> b
	Semicolon
	return
    }

    proc NextRev {_ nv} {
	upvar 1 $nv n
	RequiredLiteral next
	Anything -> n
	Semicolon
	return
    }

    # # ## ### ##### ######## #############

    proc Description {} {
	upvar 1 data data res res
	RequiredLiteral desc
	RequiredString -> d
	Call setdesc $d
	return
    }

    # # ## ### ##### ######## #############

    proc DeltaTexts {} {
	while {[OptionalNumber -> rev]} {
	    RequiredLiteral log
	    RequiredString      -> cmsg
	    if {[regexp {[\000-\010\013\014\016-\037]} $cmsg]} {
		#Rewind
		#Bad "log message for $rev contains at least one control character"
	    }

	    RequiredLiteral text
	    RequiredStringRange -> delta
	    Call extend $rev $cmsg $delta
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods, lexiographical processing

    proc Semicolon {} {
	::variable mydata
	::variable mypos

	set ok [regexp -start $mypos -indices -- {\A\s*;\s*} $mydata match]
	if {!$ok} { Expected ';' }

	SkipOver match
	return
    }

    proc RequiredLiteral {name} {
	::variable mydata
	::variable mypos

	set pattern "\\A\\s*$name\\s*"
	set ok [regexp -start $mypos -indices -- $pattern $mydata match]
	if {!$ok} { Expected '$name' }

	SkipOver match
	return
    }

    proc OptionalLiteral {name} {
	::variable mydata
	::variable mypos

	set pattern "\\A\\s*$name\\s*"
	set ok [regexp -start $mypos -indices -- $pattern $mydata match]
	if {!$ok} { return 0 }

	SkipOver match
	return 1
    }

    proc LiteralPeek {name} {
	::variable mydata
	::variable mypos

	set pattern "\\A\\s*$name\\s*"
	set ok [regexp -start $mypos -indices -- $pattern $mydata match]
	if {!$ok} { return 0 }

	# NO - SkipOver match - Only looking ahead here.
	return 1
    }

    proc RequiredNumber {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set pattern {\A\s*((\d|\.)+)\s*}
	set ok [regexp -start $mypos -indices -- $pattern $mydata match v]
	if {!$ok} { Expected id }

	Extract $v -> value
	SkipOver match
	return
    }

    proc OptionalNumber {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set pattern {\A\s*((\d|\.)+)\s*}
	set ok [regexp -start $mypos -indices -- $pattern $mydata match v]
	if {!$ok} { return 0 }

	Extract $v -> value
	SkipOver match
	return 1
    }

    proc RequiredString {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set ok [regexp -start $mypos -indices -- {\A\s*@(([^@]*(@@)*)*)@\s*} $mydata match v]
	if {!$ok} { Expected string }

	Extract $v -> value
	set value [string map {@@ @} $value]
	SkipOver match
	return
    }

    proc RequiredStringRange {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set ok [regexp -start $mypos -indices -- {\A\s*@(([^@]*(@@)*)*)@\s*} $mydata match value]
	if {!$ok} { Expected string }

	SkipOver match
	return
    }

    proc OptionalString {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set ok [regexp -start $mypos -indices -- {\A\s*@(([^@]*(@@)*)*)@\s*} $mydata match v]
	if {!$ok} { return 0 }

	Extract $v -> value
	set value [string map {@@ @} $value]
	SkipOver match
	return 1
    }

    proc Ident {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	set ok [regexp -start $mypos -indices -- {\A\s*;\s*} $mydata]
	if {$ok} { return 0 }

	set ok [regexp -start $mypos -indices -- {\A\s*([^:]*)\s*:\s*} $mydata match v]
	if {!$ok} { return 0 }

	Extract $v -> value
	SkipOver match
	return 1
    }

    proc Anything {_ v} {
	upvar 1 $v value
	::variable mydata
	::variable mypos

	regexp -start $mypos -indices -- {\A\s*([^;]*)\s*} $mydata match v

	Extract $v -> value
	SkipOver match
	return
    }

    # # ## ### ##### ######## #############
    ## Internal methods, input handling

    proc Extract {range _ v} {
	upvar 1 $v value
	::variable mydata
	struct::list assign $range s e
	set value [string range $mydata $s $e]
	return
    }

    proc SkipOver {mv} {
	# Note: The indices are absolute!, not relative to the start
	# location.
	upvar 1 $mv match
	::variable mypos
	::variable mysize
	::variable mylastpos

	struct::list assign $match s e
	#puts "<$s $e> [info level -1]"

	set  mylastpos $mypos
	set  mypos $e
	incr mypos

	log progress 2 rcs $mypos $mysize
	#puts $mypos/$mysize
	return
    }

    proc Rewind {} {
	::variable mypos
	::variable mylastpos

	set  mypos $mylastpos
	return
    }

    proc Expected {x} {
	::variable mydata
	::variable mypos
	set e $mypos ; incr e 30
	return -code error -errorcode vc::rcs::parser \
	    "Expected $x @ '[string range $mydata $mypos $e]...'"
    }

    proc Bad {x} {
	::variable mydata
	::variable mypos
	set e $mypos ; incr e 30
	return -code error -errorcode vc::rcs::parser \
	    "Bad $x @ '[string range $mydata $mypos $e]...'"
    }

    # # ## ### ##### ######## #############
    ## Setup, callbacks.

    proc Initialize {path sink} {
	::variable mypos  0
	::variable mydata [fileutil::cat -translation binary $path]
	::variable mysize [file size $path]
	::variable mysink $sink
	return
    }

    proc Call {args} {
	::variable mysink
	set cmd $mysink
	foreach a $args { lappend cmd $a }
	eval $cmd
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    typevariable mydata {} ; # Rcs archive contents to process
    typevariable mysize 0  ; # Length of contents
    typevariable mysink {} ; # Sink to report to

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::rcs {
    namespace export parser
    namespace eval parser {
	namespace import ::vc::tools::log
	log register rcs
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::rcs::parser 1.0
return
