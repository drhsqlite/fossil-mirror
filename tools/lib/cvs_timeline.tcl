# -----------------------------------------------------------------------------
# Repository management (CVS), timeline of events.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4

namespace eval ::vc::cvs::ws::timeline {}

# -----------------------------------------------------------------------------
# API

# vc::cvs::ws::timeline::add     date file revision operation author commit-msg
# vc::cvs::ws::timeline::foreach date file revision operation author commit-msg script

# Add entries to the timeline, and iterate over the timeline in proper order.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::cvs::ws::timeline::add {date file revision operation author cmsg} {
    variable timeline
    lappend  timeline($date) [list $file $revision $operation $author $cmsg]
    return
}

proc ::vc::cvs::ws::timeline::foreach {dv fv rv ov av cv script} {
    upvar 1 $dv date $fv file $rv revision $ov operation $av author $cv cmsg
    variable timeline

    ::foreach date [lsort -dict [array names timeline]] {
	# file revision operation author commitmsg
	# 0    1        2         3      4/end
	# d    e        b         c      a

	set entries [lsort -index 1 \
			 [lsort -index 0 \
			      [lsort -index 3 \
				   [lsort -index 2 \
					[lsort -index end \
					     $timeline($date)]]]]]
	#puts [join $entries \n]

	::foreach entry $entries {
	    lassign $entry file revision operation author cmsg
	    set code [catch {uplevel 1 $script} res]

	    # 0 - ok, 1 - error, 2 - return, 3 - break, 4 - continue
	    switch -- $code {
		0 {}
		1 { return -errorcode $::errorCode -errorinfo $::errorInfo -code error $res }
		2 {}
		3 { return }
		4 {}
		default {
		    return -code $code $result
		}
	    }
	}
    }
    return
}

# -----------------------------------------------------------------------------
# Internals

proc ::vc::cvs::ws::timeline::lassign {l args} {
    ::foreach v $args {upvar 1 $v $v} 
    ::foreach $args $l break
    return
}

namespace eval ::vc::cvs::ws::timeline {
    # Timeline: map (date -> list (file revision operation author commitmsg))

    variable  timeline
    array set timeline {}

    namespace export add
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::cvs::ws::timeline 1.0
return
