# -----------------------------------------------------------------------------
# Repository management (CVS)

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require fileutil              ; # Tcllib (traverse directory hierarchy)
package require vc::rcs::parser       ; # Handling the RCS archive files.
package require vc::tools::log        ; # User feedback
package require vc::cvs::cmd          ; # Access to cvs application.
package require vc::cvs::ws::files    ; # Scan CVS repository for relevant files.
package require vc::cvs::ws::timeline ; # Manage timeline of all changes.
package require struct::tree

namespace eval ::vc::cvs::ws {
    vc::tools::log::system cvs
    namespace import ::vc::tools::log::write
    namespace import ::vc::rcs::parser::process
    namespace import ::vc::cvs::cmd::dova
}

# -----------------------------------------------------------------------------
# API

# vc::cvs::ws::configure key value    - Configure the subsystem.
# vc::cvs::ws::check     src mv       - Check if src is a CVS repository directory.
# vc::cvs::ws::begin     src          - Start new workspace and return the top-
#                                       most directory co'd files are put into.
# vc::cvs::ws::ncsets    ?-import?    - Retrieve number of csets (all/to import)
# vc::cvs::ws::foreach   csvar script - Run the script for each changeset, the
#                                       id of the current changeset stored in
#                                       the variable named by csvar.
# vc::cvs::ws::done                   - Close workspace and delete it.
# vc::cvs::ws::isadmin path           - Check if path is an admin file of CVS
# vc::cvs::ws::checkout id            - Have workspace contain the changeset id.
#
# Configuration keys:
#
# -project path - Sub directory under 'src' to limit the import to.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::cvs::ws::configure {key value} {
    variable project

    switch -exact -- $key {
	-project { set project $value }
	default {
	    return -code error "Unknown switch $key, expected \
                                   -project"
	}
    }
    return
}

proc ::vc::cvs::ws::check {src mv} {
    variable project
    upvar 1 $mv msg
    if {
	![fileutil::test $src         erd msg "CVS Repository"] ||
	![fileutil::test $src/CVSROOT erd msg "CVS Admin directory"] ||
	(($project ne "") &&
	 ![fileutil::test $src/$project erd msg "Project directory"])
    } {
	return 0
    }
    return 1
}

proc ::vc::cvs::ws::begin {src} {
    if {![check $src msg]} { return -code error $msg }

    DefBase $src
    MakeTimeline [ScanArchives [files::find [RootPath]]]

    # OLD api calls ... TODO rework for more structure ...
    csets    ; # Group changes into sets
    rtree    ; # Build revision tree (trunk only right now).

    return [MakeWorkspace]
}

proc ::vc::cvs::ws::done {} {
    variable            workspace
    file delete -force $workspace
    return
}

proc ::vc::cvs::ws::foreach {cv script} {
    # OLD api ... TODO inline
    uplevel 1 [list ::vc::cvs::ws::foreach_cset $cv 0 $script]
}

proc ::vc::cvs::ws::ncsets {args} {
    variable ncs
    variable ntrunk

    if {[llength $args] > 1} {
	return -code error "wrong#args: Expected ?-import?"
    } elseif {[llength $args] == 1} {
	if {[set k [lindex $args 0]] ne "-import"} {
	    return -code "Unknown switch $k, expected -import"
	} else {
	    return $ntrunk
	}
    }

    return  $ncs
}

proc ::vc::cvs::ws::isadmin {path} {
    # Check if path is a CVS admin file.
    if {[string match CVS/*   $path]} {return 1}
    if {[string match */CVS/* $path]} {return 1}
    return 0
}

proc ::vc::cvs::ws::checkout {id} {
    variable workspace ; cd $workspace
    wssetup $id ; # OLD api ... TODO inline
}

# -----------------------------------------------------------------------------
# Internals - Old API for now.

proc ::vc::cvs::ws::DefBase {path} {
    variable project
    variable base

    set base $path

    write 0 cvs "Base:    $base"
    if {$project eq ""} {
	write 0 cvs "Project: <ALL>"
    } else {
	write 0 cvs "Project: $project"
    }
    return
}

proc ::vc::cvs::ws::RootPath {} {
    variable project
    variable base

    if {$project eq ""} {
	return $base
    } else {
	return $base/$project
    }
}

# Scan repository, collect archives, parse them, and collect revision
# information (file, revision -> date, author, commit message)

proc ::vc::cvs::ws::ScanArchives {files} {
    write 0 cvs "Scanning archives ..."

    set d [RootPath]
    set r {}
    set n 0

    ::foreach {rcs f} $files {
	write 1 cvs "Archive $rcs"
	# Get the meta data we need (revisions, timeline, messages).
	lappend r $f [process $d/$rcs]
	incr    n
    }

    write 0 cvs "Processed [NSIPL $n file]"
    return $r
}

proc ::vc::cvs::ws::MakeTimeline {meta} {
    write 0 cvs "Generating coalesced timeline ..."

    set n 0
    ::foreach {f meta} $meta {
	array set md   $meta
	array set date $md(date)
	array set auth $md(author)
	array set cmsg $md(commit)
	array set stat $md(state)

	::foreach rev [lsort -dict [array names date]] {
	    set operation [Operation $rev $stat($rev)]
	    NoteDeadRoots $f $rev $operation
	    timeline::add $date($rev) $f $rev $operation $auth($rev) $cmsg($rev)
	    incr n
	}
	#B Extend branch management

	unset md
	unset date
	unset auth
	unset cmsg
	unset stat
    }

    write 0 cvs "Generated [NSIPL $n entry entries]"
    return
}

proc ::vc::cvs::ws::NoteDeadRoots {f rev operation} {
    # A dead-first revision is rev 1.1 with op R. For an example see
    # the file memchan/DEPENDENCIES. Such a file seems to exist only!
    # on its branch. The branches information is set on the revision
    # (extend rcsparser!), symbols has a tag, refering to a branch,
    # possibly magic.

    if {($rev eq "1.1") && ($operation eq "R")} {
	write 2 cvs "Dead root revision: $f"
    }
    return
}

proc ::vc::cvs::ws::Operation {rev state} {
    if {$state eq "dead"} {return "R"} ; # Removed
    if {$rev   eq "1.1"}  {return "A"} ; # Added
    return "M"                         ; # Modified
}

proc ::vc::cvs::ws::MakeWorkspace {} {
    variable project
    variable workspace [fileutil::tempfile importF_cvs_ws_]

    set w $workspace
    if {$project ne ""} { append w /$project }

    file delete $workspace
    file mkdir  $w

    write 0 cvs "Workspace:  $workspace"
    return $w
}


# Group single changes into changesets

proc ::vc::cvs::ws::csets {} {
    variable timeline
    variable csets
    variable ncs
    variable cmap

    array unset csets * ; array set csets {}
    array unset cmap  * ; array set cmap  {}
    set ncs 0

    write 0 cvs "Generating changesets from timeline"

    CSClear
    timeline::foreach date file revision operation author cmsg {
	# API adaption
	set entry [list $operation $date $author $revision $file $cmsg]

	if {![CSNone] && [CSNew $entry]} {
	    CSSave
	    CSClear
	}
	CSAdd $entry
    }

    write 0 cvs "Found [NSIPL [array size csets] changeset]"
    return
}


namespace eval ::vc::cvs::ws {
    # Changeset data:
    # ncs:   Counter-based id generation
    # csets: id -> (user commit start end depth (file -> (op rev)))

    variable ncs      ; set       ncs   0  ; # Counter for changesets
    variable csets    ; array set csets {} ; # Changeset data
}

# Building the revision tree from the changesets.
# Limitation: Currently only trunk csets is handled.
# Limitation: Dead files are not removed, i.e. no 'R' actions right now.

proc ::vc::cvs::ws::rtree {} {
    variable csets
    variable rtree {}
    variable ntrunk 0

    write 0 cvs "Extracting the trunk"

    set rtree [struct::tree ::vc::cvs::ws::RT]
    $rtree rename root 0 ; # Root is first changeset, always.
    set trunk 0
    set ntrunk 1 ; # Root is on the trunk.
    set b      0 ; # No branch csets found yet.

    # Extracting the trunk is easy, simply by looking at the involved
    # version numbers. 

    ::foreach c [lrange [lsort -integer [array names csets]] 1 end] {
	::foreach {u cm s e rd f} $csets($c) break

	# Ignore branch changes, just count them for the statistics.
	if {$rd != 2} {
	    incr b
	    continue
	}

	# Trunk revision, connect to, and update the head.
	$rtree insert $trunk end $c
	set trunk $c
	incr ntrunk
    }

    write 0 cvs "Processed $ntrunk trunk  [expr {($ntrunk == 1) ? "changeset" : "changesets"}]"
    write 0 cvs "Ignored   $b branch [expr {($b == 1) ? "changeset" : "changesets"}]"
    return
}

namespace eval ::vc::cvs::ws {
    # Tree holding trunk and branch information (struct::tree).
    # Node names are cset id's.

    variable rtree {}
    variable ntrunk 0
}

proc ::vc::cvs::ws::wssetup {c} {
    variable csets
    variable base
    variable project

    # pwd = workspace

    ::foreach {u cm s e rd fs} $csets($c) break

    write 1 cvs "@  $s"

    ::foreach l [split [string trim $cm] \n] {
	write 1 cvs "|  $l"
    }

    ::foreach {f or} $fs {
	::foreach {op r} $or break
	write 2 cvs "$op  $f $r"

	if {$op eq "R"} {
	    # Remove file from workspace. Prune empty directories.
	    #
	    # NOTE: A dead-first file (rev 1.1 dead) will never have
	    # existed.
	    #
	    # NOTE: Logically empty directories still physically
	    # contain the CVS admin directory, hence the check for ==
	    # 1, not == 0. There might also be hidden files, we count
	    # them as well. Always hidden are . and .. and they do not
	    # count as user file.

	    file delete $f
	    set fd [file dirname $f]
	    if {
		([llength [glob -nocomplain -directory              $fd *]] == 1) &&
		([llength [glob -nocomplain -directory -type hidden $fd *]] == 2)
	    } {
		file delete -force $fd
	    }
	} else {
	    # Added or modified, put the requested version of the file
	    # into the workspace.

	    if {$project ne ""} {set f $project/$f}
	    if {[catch {
		dova -d $base co -r $r $f
	    } msg]} {
		if {[string match {*invalid change text*} $msg]} {
		    # The archive of the file is corrupted and the
		    # chosen version not accessible due to that. We
		    # report the problem, but otherwise ignore it. As
		    # a consequence the destination repository will not
		    # contain the full history of the named file. By
		    # ignoring the problem we however get as much as
		    # is possible.

		    write 0 cvs "EE Corrupted archive file. Inaccessible revision."
		    continue
		}
		return -code error $msg
	    }
	}
    }

    # Provide metadata about the changeset the backend may wish to have
    return [list $u $s $cm]
}

proc ::vc::cvs::ws::foreach_cset {cv node script} {
    upvar 1 $cv c
    variable rtree

    set c $node
    while {1} {
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

	# Stop on reaching the head.
	if {![llength [$rtree children $c]]} break

	#puts <[$rtree children $c]>

	# Go to next child in trunk (leftmost).
	set c [lindex [$rtree children $c] 0]
    }
    return
}

# -----------------------------------------------------------------------------
# Internal helper commands: Changeset inspection and construction.

proc ::vc::cvs::ws::CSClear {} {
    upvar 1 start start end end cm cm user user files files lastd lastd

    set start {}
    set end   {}
    set cm    {}
    set user  {}
    set lastd {}
    array unset files *
    array set files {}
    return
}

proc ::vc::cvs::ws::CSNone {} {
    upvar 1 start start
    return [expr {$start eq ""}]
}

proc ::vc::cvs::ws::CSNew {entry} {
    upvar 1 start start end end cm cm user user files files lastd lastd reason reason

    #puts -nonewline stdout . ; flush stdout

    ::foreach {op ts a rev f ecm} $entry break

    # User change
    if {$a ne $user} {set reason user ; return 1}

    # File already in current cset
    if {[info exists files($f)]} {set reason file ; return 1}

    # Current cset trunk/branch different from entry.
    set depth [llength [split $rev .]]
    if {($lastd == 2) != ($depth == 2)} {set reason depth/$lastd/$depth/($rev)/$f ; return 1}

    # Commit message changed
    if {$cm ne $ecm} {set reason cmsg\ <<$ecm>> ; return 1}

    # Everything is good, still the same cset
    return 0
}

proc ::vc::cvs::ws::CSSave {} {
    variable cmap
    variable csets
    variable ncs
    upvar 1 start start end end cm cm user user files files lastd lastd

    set csets($ncs) [list $user $cm $start $end $lastd [array get files]]

    # Record which revisions of a file are in what csets
    ::foreach {f or} [array get files] {
	::foreach {_ rev} $or break
	set cmap([list $f $rev]) $ncs
    }

    #CSDump $ncs

    incr ncs
    return
}

proc ::vc::cvs::ws::CSAdd {entry} {
    upvar 1 start start end end cm cm user user files files lastd lastd

    ::foreach {op ts a rev f ecm} $entry break

    if {$start eq ""} {set start $ts}
    set end       $ts
    set cm        $ecm
    set user      $a
    set files($f) [list $op $rev]
    set lastd     [llength [split $rev .]]
    return
}

proc ::vc::cvs::ws::CSDump {c} {
    variable csets
    ::foreach {u cm s e rd f} $csets($c) break

    puts "$u $s"; regsub -all {.} $u { } b
    puts "$b $e"
    ::foreach {f or} $f {
	::foreach {o r} $or break
	puts "$b $o $f $r"
    }
    return
}

proc ::vc::cvs::ws::NSIPL {n singular {plural {}}} {
    return "$n [SIPL $n $singular $plural]"
}
proc ::vc::cvs::ws::SIPL {n singular {plural {}}} {
    if {$n == 1} {return $singular}
    if {$plural eq ""} {set plural ${singular}s}
    return $plural
}

# -----------------------------------------------------------------------------

namespace eval ::vc::cvs::ws {
    variable base      {} ; # Toplevel repository directory
    variable project   {} ; # Sub directory to limit the import to.
    variable workspace {} ; # Directory to checkout changesets to.

    namespace export configure begin done foreach ncsets checkout
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::cvs::ws 1.0
return
