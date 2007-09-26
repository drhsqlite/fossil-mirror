
package require struct::set
package require vc::cvs::ws::branch

namespace eval ::vc::cvs::ws::sig::branch {
    namespace import ::vc::cvs::ws::branch::*
}

# Save the mapping from changesets to file/rev signatures, and further
# remember all the csets a specific file/rev combination belongs to.

proc ::vc::cvs::ws::sig::def {id parent added changed removed} {
    variable sig
    variable csl

    array set new $sig($parent)
    array set new $added
    array set new $changed
    foreach {f r} $removed {catch {unset new($f)}}
    set sig($id) [DictSort [array get new]]

    foreach {f r} [array get new] {
	lappend csl($f,$r) $id
    }
    return
}

proc ::vc::cvs::ws::sig::next {id added changed removed tag ts} {
    variable sig
    array set rev $sig($id)

    foreach {f r} [concat $changed $removed] {
	if {![info exists rev($f)] || ![branch::successor $r $rev($f)]} {
	    return 0
	}
    }

    if {[llength $added]} {
	# Check that added files belong to the branch too!
	if {$tag ne [branch::has $ts $added]} {
	    return 0
	}
    }
    return 1
}


proc ::vc::cvs::ws::sig::find {id sig} {
    set cslist [Cut $id [Find $sig]]

    if {[llength $cslist] < 1} {
	puts "NO ROOT"
	# Deal how?
	# - Abort
	# - Ignore this changeset and try the next one
	#   (Which has higher probability of not matching as it might
	#    be the successor in the branch to this cset and not a base).
	exit
    } elseif {[llength $cslist] > 1} {
	puts "AMBIGOUS. Following csets match root requirements:"
	# Deal how? S.a.
	puts \t[join $cslist \n\t]
	exit
    }

    set r [lindex $cslist 0]
    #puts "ROOT = $r"
    return $r
}

proc ::vc::cvs::ws::sig::Cut {id cslist} {
    # Changesets have to be before id! This makes for another
    # intersection, programmatic.

    set res {}
    foreach c $cslist {
	if {$c >= $id} continue
	lappend res $c
    }
    return $res
}

proc ::vc::cvs::ws::sig::Find {sig} {
    # Locate all changesets which contain the given signature.
    variable csl

    set res {}
    set first 1
    foreach {f r} $sig {
	#puts $f/$r?
	# Unknown file not used anywhere
	if {![info exists csl($f,$r)]} {return {}}
	puts $f/$r\t=\t($csl($f,$r))*($res)/$first

	if {$first} {
	    set res $csl($f,$r)
	    set first 0
	    #puts F($res)
	} else {
	    set new [struct::set intersect $res $csl($f,$r)]
	    set rv $r
	    while {![llength $new]} {
		# Assume that the problem file was added and as such
		# does not exist yet at the root revision. However its
		# root should exist, and some point.

	       set rv [branch::revroot $rv]
	       if {$rv eq ""} {
		   puts BREAK/\t($f\ $r)
		   exit
	       }
	       if {![info exists csl($f,$rv)]} {return {}}
	       #puts $f/$r\t=\t($csl($f,$rv))
	       set new [struct::set intersect $res $csl($f,$rv)]
	    }
	    set res $new
	    #puts R($res)
	    #if {![llength $res]} {return {}}
	}
    }
    return $res
}

proc ::vc::cvs::ws::sig::DictSort {dict} {
    array set a $dict
    set r {}
    foreach k [lsort [array names a]] {
	lappend r $k $a($k)
    }
    return $r
}


namespace eval ::vc::cvs::ws::sig {
    variable  sig ; # cset id -> signature
    array set sig {{} {}}
    variable  csl ; # file x rev -> list (cset id)
    array set csl {}

    namespace export def find next
}

package provide vc::cvs::ws::sig 1.0
return
