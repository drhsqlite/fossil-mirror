
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

    #puts sig::next/$ts
    foreach {f r} [concat $changed $removed] {
	if {![info exists rev($f)]}              {

	    # A file missing in the candidate parent changeset is
	    # _not_ a reason to reject it, at least not immediately.
	    # The code generating the timeline entries has only
	    # partial information and is prone to misclassify files
	    # added to branches as changed instead of added. Thus we
	    # move this file to the list of added things and check it
	    # again as part of that, see below.

	    lappend added $f $r
	    continue
	}
	if {[branch::rootSuccessor $r $rev($f)]} continue
	if {![branch::successor    $r $rev($f)]} {
	    #puts "not-successor($r of $rev($f))"
	    return 0
	}
    }

    if {[llength $added]} {
	# Check that added files belong to the branch too!
	if {$tag ne [branch::has $ts $added]} {
	    #puts "not-added-into-same-branch"
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

    # First we try to the exact changeset, by intersecting the
    # live-intervals for all file revisions found in the
    # signature. This however may fail, as CVS is able to contain
    # a-causal branch definitions.

    # Example: sqlite, branch "gdbm-branch".

    # File 'db.c', branch 1.6.2, root 1.6, entered on Jan 31, 2001.
    # Then 'dbbegdbm.c',  1.1.2, root 1.1, entered on Oct 19, 2000.

    # More pertinent, revision 1.2 was entered Jan 13, 2001,
    # i.e. existed before Jan 31, before the branchwas actually
    # made. Thus it is unclear why 1.1 is in the branch instead.

    # An alternative complementary question would be how db.c 1.6
    # ended up in a branch tag created before Jan 13, when this
    # revision did not exist yet.

    # So, CVS repositories can be a-causal when it comes to branches,
    # at least in the details. Therefore while try for an exact result
    # first we do not fail if that fails, but use a voting scheme as
    # fallback which answers the question about which changeset is
    # acceptable to the most file revisions in the signature.

    # Note that multiple changesets are ok at this level and are
    # simply returned.

    set res [Intersect $sig]
    puts Exact=($res)

    if {[llength $res]} { return $res }

    set res [Vote $sig]
    puts Vote=($res)

    return $res
}


proc ::vc::cvs::ws::sig::Intersect {sig} {
    variable csl

    set res {}
    set first 1
    foreach {f r} $sig {
	#puts $f/$r?
	# Unknown file not used anywhere
	if {![info exists csl($f,$r)]} {return {}}
	#puts $f/$r\t=\t($csl($f,$r))*($res)/$first

	if {$first} {
	    set res $csl($f,$r)
	    set first 0
	    #puts F($res)
	} else {
	    set res [struct::set intersect $res $csl($f,$r)]
	    #puts R($res)
	    if {![llength $res]} {return {}}
	}
    }
    return $res
}


proc ::vc::cvs::ws::sig::Vote {sig} {
    variable csl

    # I. Accumulate votes.
    array set v {}
    foreach {f r} $sig {
	# Unknown revisions do not vote.
	if {![info exists csl($f,$r)]} continue
	foreach c $csl($f,$r) {
	    if {[info exists v($c)]} {
		incr v($c)
	    } else {
		set v($c) 1
	    }
	}
    }

    # Invert index for easier finding the max, compute the max at the
    # same time.
    array set tally {}
    set max -1
    foreach {c n} [array get v] {
	lappend tally($n) $c
	if {$n > $max} {set max $n}
    }

    #parray tally
    puts Max=$max

    # Return the changesets having the most votes.
    return $tally($max)
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
