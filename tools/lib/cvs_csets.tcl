# -----------------------------------------------------------------------------
# Repository management (CVS), Changeset grouping and storage.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::cvs::ws::sig      ; # Changeset file/rev signatures

namespace eval ::vc::cvs::ws::csets::Current {}
namespace eval ::vc::cvs::ws::csets::sig {
    namespace import ::vc::cvs::ws::sig::*
}

# -----------------------------------------------------------------------------
# API

# vc::cvs::ws::csets::init   - Initialize accumulator
# vc::cvs::ws::csets::add    - Add timeline entry to accumulor, may generate new cset
# vc::cvs::ws::csets::done   - Complete cset generation.
#
# vc::cvs::ws::csets::get id - Get data of a cset.
# vc::cvs::ws::csets::num    - Get number of csets.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::cvs::ws::csets::init {} {
    variable ncs 0
    Current::Clear
    return
}

proc ::vc::cvs::ws::csets::add {date file revision operation author cmsg} {
    if {![Current::Empty] && [Current::New $file $revision $author $cmsg]} {
	Save [Current::Complete]
    }
    Current::Add $date $file $revision $operation $author $cmsg
    return
}

proc ::vc::cvs::ws::csets::done {} {
    if {![Current::Empty]} {
	Save [Current::Complete]
    }
    return
}

proc ::vc::cvs::ws::csets::get {id} {
    variable csets
    return  $csets($id)
}


proc ::vc::cvs::ws::csets::DUMP {id} {
    puts /${id}/_________________
    array set cs [get $id]
    parray cs
    return
}

proc ::vc::cvs::ws::csets::num {} {
    variable csets
    return [array size csets]
}

proc ::vc::cvs::ws::csets::isTrunk {id} {
    variable csets
    array set cs $csets($id)
    return [expr {$cs(lastd) == 2}]
}

proc ::vc::cvs::ws::csets::setParentOf {id parent} {
    variable csets
    lappend  csets($id) parent $parent

    array set cs $csets($id)
    sig::def            $id $parent $cs(added) $cs(changed) $cs(removed)
    return
}

proc ::vc::cvs::ws::csets::parentOf {id} {
    variable      csets
    array set cs $csets($id)
    return   $cs(parent)
}

proc ::vc::cvs::ws::csets::sameBranch {id parent tag} {
    variable      csets
    array set cs $csets($id)
    return [sig::next $parent $cs(added) $cs(changed) $cs(removed) $tag $cs(date)]
}

# -----------------------------------------------------------------------------
# Internal helper commands: Changeset inspection and construction.

proc ::vc::cvs::ws::csets::Save {data} {
    variable csets
    variable ncs

    set csets($ncs) $data
    incr ncs
    return
}

proc ::vc::cvs::ws::csets::Current::Clear {} {
    variable    start   {} ; # date the changeset begins
    variable    cmsg    {} ; # commit message of the changeset
    variable    author  {} ; # user creating the changeset
    variable    lastd   {} ; # version depth of last added file.
    variable    removed {} ; # file -> revision of removed files.
    variable    added   {} ; # file -> revision of added files.
    variable    changed {} ; # file -> revision of modified files.
    variable    files
    array unset files *
    array set   files {}   ; # file -> revision
    return
}

proc ::vc::cvs::ws::csets::Current::Empty {} {
    variable start
    return [expr {$start eq ""}]
}

proc ::vc::cvs::ws::csets::Current::New {nfile nrevision nauthor ncmsg} {
    upvar 1 reason reason
    variable cmsg
    variable author
    variable lastd
    variable files

    # User change
    if {$nauthor ne $author} {
	set reason user
	return 1
    }

    # File already in current cset
    if {[info exists files($nfile)]} {
	set reason file
	return 1
    }

    # Current cset trunk/branch different from entry.
    set ndepth [llength [split $nrevision .]]
    if {($lastd == 2) != ($ndepth == 2)} {
	set reason depth/$lastd/$ndepth/($nrevision)/$nfile
	return 1
    }

    # Commit message changed
    if {$ncmsg ne $cmsg} {
	set reason cmsg/<<$ncmsg>>
	return 1
    }

    # The new entry still belongs to the current changeset
    return 0
}

proc ::vc::cvs::ws::csets::Current::Add {ndate nfile nrevision noperation nauthor ncmsg} {
    variable start
    variable cmsg
    variable author
    variable lastd
    variable removed
    variable added
    variable changed
    variable files

    if {$start eq ""} {set start $ndate}
    set cmsg          $ncmsg
    set author        $nauthor
    set lastd         [llength [split $nrevision .]]
    set files($nfile) $nrevision

    if {$noperation eq "R"} {
	lappend removed $nfile $nrevision
    } elseif {$noperation eq "A"} {
	lappend added   $nfile $nrevision
    } else {
	lappend changed $nfile $nrevision
    }
    return
}

proc ::vc::cvs::ws::csets::Current::Complete {} {
    variable start
    variable cmsg
    variable author
    variable lastd
    variable removed
    variable added
    variable changed

    set res [list \
		date    $start \
		author  $author \
		cmsg    [string trim $cmsg] \
		removed $removed \
		added   $added \
		changed $changed \
		lastd   $lastd]
    Clear
    return $res
}

# -----------------------------------------------------------------------------
# Internals

namespace eval ::vc::cvs::ws::csets {

    # Cset storage

    # csets: id -> dict
    # dict: date
    #       author
    #       csmg
    #       removed
    #       added
    #       changed
    #       lastd

    variable  ncs   0  ; # Counter for changesets
    variable  csets
    array set csets {} ; # Changeset data

    # Data of the current changeset built from timeline entries.
    namespace eval Current {
	variable  start   {} ; # date the changeset begins
	variable  cmsg    {} ; # commit message of the changeset
	variable  author  {} ; # user creating the changeset
	variable  lastd   {} ; # version depth of last added file.
	variable  removed {} ; # file -> revision of removed files.
	variable  added   {} ; # file -> revision of added files.
	variable  changed {} ; # file -> revision of modified files.
	variable  files
	array set files {}   ; # file -> revision
    }

    namespace export init add done get num isTrunk setParentOf parentOf sameBranch
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::cvs::ws::csets 1.0
return
