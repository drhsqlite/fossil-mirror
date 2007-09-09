# -----------------------------------------------------------------------------
# Repository management (CVS)

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require fileutil       ; # Tcllib (traverse directory hierarchy)
package require rcsparser      ; # Handling the RCS archive files.
package require vc::tools::log ; # User feedback
package require struct::tree

namespace eval ::cvs {
    vc::tools::log::system cvs
    namespace import ::vc::tools::log::write
}

# -----------------------------------------------------------------------------
# API

# Define repository directory.

proc ::cvs::at {path} {
    variable base [file normalize $path]
    write 0 cvs "Base: $base"
    return
}

namespace eval ::cvs {
    # Toplevel repository directory
    variable base {}
}

# Scan repository, collect archives, parse them, and collect revision
# information (file, revision -> date, author, commit message)

proc ::cvs::scan {} {
    variable base
    variable npaths
    variable rpaths
    variable timeline

    write 0 cvs {Scanning directory hierarchy}

    set n 0
    foreach rcs [fileutil::findByPattern $base -glob *,v] {
	set rcs [fileutil::stripPath $base $rcs]
	# Now rcs is relative to base

	write 1 cvs "Archive $rcs"

	if {[string match CVSROOT* $rcs]} {
	    write 2 cvs {Ignored. Administrative file}
	    continue
	}

	# Derive the regular path from the rcs path. Meaning: Chop of
	# the ",v" suffix, and remove a possible "Attic".
	set f [string range $rcs 0 end-2]
	if {"Attic" eq [lindex [file split $rcs] end-1]} {
	    set f [file join [file dirname [file dirname $f]] [file tail $f]]
	    if {[file exists $base/$f,v]} {
		# We have a regular archive and an Attic archive
		# refering to the same user visible file. Ignore the
		# file in the Attic.

		write 2 cvs "Ignored. Attic superceded by regular archive"

		# TODO/CHECK. My method of co'ing exact file revisions
		# per the info in the collected csets has the flaw
		# that I may have to know exactly when what archive
		# file to use, see above. It might be better to use
		# the info only to gather when csets begin and end,
		# and then to co complete slices per exact timestamp
		# (-D) instead of file revisions (-r). The flaw in
		# that is that csets can occur in the same second
		# (trf, memchan - check for examples). For that exact
		# checkout may be needed to recreate exact sequence of
		# changes. Grr. Six of one ...

		continue
	    }
	}

	# Get the meta data we need (revisions, timeline, messages).
	set meta [::rcsparser::process $base/$rcs]

	set npaths($rcs) $f
	set rpaths($f) $rcs

	array set p $meta

	foreach {rev ts} $p(date) {_ a} $p(author) {_ cm} $p(commit) {_ st} $p(state) {
	    set op [expr {($rev eq "1.1") ? "A" : "M"}]
	    if {$st eq "dead"} {set op "R"}

	    # A dead-first revision is rev 1.1 with op R. For an
	    # example see the file memchan/DEPENDENCIES. Such a file
	    # seems to exist only! on its branch. The branches
	    # information is set on the revision (extend rcsparser!),
	    # symbols has a tag, refering to a branch, possibly magic.

	    if {($rev eq "1.1") && ($op eq "R")} {
		write 2 cvs {Dead root revision}
	    }

	    lappend timeline($ts) [list $op $ts $a $rev $f $cm]
	}

	#unset p(commit)
	#parray p

	incr n
    }

    write 0 cvs "Processed $n [expr {($n == 1) ? "file" : "files"}]"
    return
}

namespace eval ::cvs {
    # Path mappings. npaths: rcs file  -> user file
    #                rpaths: user file -> rcs file, dead-status

    variable npaths   ; array set npaths   {}
    variable rpaths   ; array set rpaths   {}

    # Timeline: tstamp -> (op, tstamp, author, revision, file, commit message)

    variable timeline ; array set timeline {}
}

# Group single changes into changesets

proc ::cvs::csets {} {
    variable timeline
    variable csets
    variable ncs
    variable cmap

    array unset csets * ; array set csets {}
    array unset cmap  * ; array set cmap  {}
    set ncs 0

    write 0 cvs "Processing timeline"

    set n 0
    CSClear
    foreach ts [lsort -dict [array names timeline]] {

	# op tstamp author revision file commit
	# 0  1      2      3        4    5/end
	# b         c                    a

	set entries [lsort -index 2 [lsort -index 0 [lsort -index end $timeline($ts)]]]
	#puts [join $entries \n]

	foreach entry  $entries {
	    if {![CSNone] && [CSNew $entry]} {
		CSSave
		CSClear
		#puts ==\n$reason
	    }
	    CSAdd $entry
	    incr n
	}
    }

    write 0 cvs "Processed $n [expr {($n == 1) ? "entry" : "entries"}]"

    set n [array size csets]
    write 0 cvs "Found     $n [expr {($n == 1) ? "changeset" : "changesets"}]"
    return
}


namespace eval ::cvs {
    # Changeset data:
    # ncs:   Counter-based id generation
    # csets: id -> (user commit start end depth (file -> (op rev)))

    variable ncs      ; set       ncs   0  ; # Counter for changesets
    variable csets    ; array set csets {} ; # Changeset data
}

# Building the revision tree from the changesets.
# Limitation: Currently only trunk csets is handled.
# Limitation: Dead files are not removed, i.e. no 'R' actions right now.

proc ::cvs::rtree {} {
    variable csets
    variable rtree {}
    variable ntrunk 0

    write 0 cvs "Extracting the trunk"

    set rtree [struct::tree ::cvs::RT]
    $rtree rename root 0 ; # Root is first changeset, always.
    set trunk 0
    set ntrunk 1 ; # Root is on the trunk.
    set b      0 ; # No branch csets found yet.

    # Extracting the trunk is easy, simply by looking at the involved
    # version numbers. 

    foreach c [lrange [lsort -integer [array names csets]] 1 end] {
	foreach {u cm s e rd f} $csets($c) break

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

namespace eval ::cvs {
    # Tree holding trunk and branch information (struct::tree).
    # Node names are cset id's.

    variable rtree {}
    variable ntrunk 0
}

proc ::cvs::workspace {} {
    variable cwd [pwd]
    variable workspace [fileutil::tempfile importF_cvs_ws_]
    file delete $workspace
    file mkdir  $workspace

    write 0 cvs "Workspace:  $workspace"

    cd     $workspace ; # Checkouts go here.
    return $workspace
}

proc ::cvs::wsignore {path} {
    # Ignore CVS admin files.
    if {[string match */CVS/* $path]} {return 1}
    return 0
}

proc ::cvs::wsclear {} {
    variable cwd
    variable workspace
    cd $cwd
    file delete -force $workspace
    return
}

proc ::cvs::wssetup {c} {
    variable csets
    variable cvs
    variable base

    # pwd = workspace

    foreach {u cm s e rd fs} $csets($c) break

    write 1 cvs "@  $s"

    foreach l [split [string trim $cm] \n] {
	write 1 cvs "|  $l"
    }

    foreach {f or} $fs {
	foreach {op r} $or break
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

	    if {[catch {
		exec $cvs -d $base co -r $r $f
	    } msg]} {
		if {[string match {*invalid change text*} $msg]} {
		    # The archive of the file is corrupted and the
		    # chosen version not accessible due to that. We
		    # report the problem, but otherwise ignore it. As
		    # a consequence the fossil repository will not
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
    return [list $u $cm $s]
}

namespace eval ::cvs {
    # CVS application
    # Workspace where checkouts happen
    # Current working directory to go back to after the import.

    variable cvs       [auto_execok cvs]
    variable workspace {}
    variable cwd       {}
}

proc ::cvs::foreach_cset {cv node script} {
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

proc ::cvs::root {} {
    return 0
}

proc ::cvs::ntrunk {} {
    variable ntrunk
    return  $ntrunk
}

proc ::cvs::ncsets {} {
    variable ncs
    return  $ncs
}

proc ::cvs::uuid {c uuid} {
    variable rtree
    $rtree set $c uuid $uuid
    return
}

# -----------------------------------------------------------------------------
# Internal helper commands: Changeset inspection and construction.

proc ::cvs::CSClear {} {
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

proc ::cvs::CSNone {} {
    upvar 1 start start
    return [expr {$start eq ""}]
}

proc ::cvs::CSNew {entry} {
    upvar 1 start start end end cm cm user user files files lastd lastd reason reason

    #puts -nonewline stdout . ; flush stdout

    foreach {op ts a rev f ecm} $entry break

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

proc ::cvs::CSSave {} {
    variable cmap
    variable csets
    variable ncs
    upvar 1 start start end end cm cm user user files files lastd lastd

    set csets($ncs) [list $user $cm $start $end $lastd [array get files]]

    # Record which revisions of a file are in what csets
    foreach {f or} [array get files] {
	foreach {_ rev} $or break
	set cmap([list $f $rev]) $ncs
    }

    #CSDump $ncs

    incr ncs
    return
}

proc ::cvs::CSAdd {entry} {
    upvar 1 start start end end cm cm user user files files lastd lastd

    foreach {op ts a rev f ecm} $entry break

    if {$start eq ""} {set start $ts}
    set end       $ts
    set cm        $ecm
    set user      $a
    set files($f) [list $op $rev]
    set lastd     [llength [split $rev .]]
    return
}

proc ::cvs::CSDump {c} {
    variable csets
    foreach {u cm s e rd f} $csets($c) break

    puts "$u $s"; regsub -all {.} $u { } b
    puts "$b $e"
    foreach {f or} $f {
	foreach {o r} $or break
	puts "$b $o $f $r"
    }
    return
}

# -----------------------------------------------------------------------------
# Ready

package provide cvs 1.0
return
