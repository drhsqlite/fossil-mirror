#!/bin/sh
# -*- tcl -*- \
exec tclsh "$0" ${1+"$@"}

# -----------------------------------------------------------------------------
# Make private packages accessible.

lappend auto_path [file join [file dirname [info script]] lib]
package require rcsparser
package require fileutil

# -----------------------------------------------------------------------------
# Repository management (CVS)

namespace eval ::cvs {
    variable base   ; set       base   {} ; # Repository toplevel directory.
    variable npaths ; array set npaths {} ; # path -> actual path mapping.
    variable rpaths ; array set rpaths {} ; # path -> rcs file mapping.
    variable cmsg   ; array set cmsg   {} ; # Cache of commit messages.
}

proc ::cvs::hextime {hex} {
    set t 0
    foreach d [string map {
	a 10 b 11 c 12 d 13 e 14 f 15
	A 10 B 11 C 12 D 13 E 14 F 15
    } [split $hex {}]] {
	set t [expr {($t << 4) + $d}];#horner
    }
    return $t
}

proc ::cvs::at {path} {
    variable base $path
    return
}

proc ::cvs::cmsg {path rev} {
    variable cmsg
    set key [list $path $rev]

    if {![info exists cmsg($key)]} {
	set rcs [cvs::rcsfile $path]

	#puts stderr "scan $path => $rcs"

	array set p [::rcsparser::process $rcs]

	foreach {r msg} $p(commit) {
	    set cmsg([list $path $r]) $msg
	}

	if {![info exists cmsg($key)]} {
	    return -code error "Bogus revision $rev of file $path"
	}
    }

    return $cmsg($key)
}

proc ::cvs::norm {path} {
    variable npaths
    if {![info exists npaths($path)]} {
	set npaths($path) [NormFile $path]
    }
    return $npaths($path)
}

proc ::cvs::NormFile {path} {
    variable base

    set f $base/$path,v
    if {[::file exists $f]} {return $path}

    set hd [::file dirname $path]
    set tl [::file tail    $path]

    set f $base/$hd/Attic/$tl,v
    if {[::file exists $f]} {return $path}

    # Bad. The dir can be truncated, i.e. it may not be an exact
    # subdirectory of base, but deeper inside, with parents between it
    # and base left out. Example (from the tcllib history file):
    #
    # M3f1d1245|afaupell|<remote>|ipentry|1.2|ChangeLog
    # The correct path is 'tklib/modules/ipentry'.
    # This we have to resolve too.

    # normalize dance - old fileutil, modern fileutil (cvs head) doesn't do that.
    set bx [file normalize $base]
    foreach c [fileutil::findByPattern $bx -glob $hd] {
	set cx [fileutil::stripPath $bx $c]
	set c  $base/$cx

	set f $c/$tl,v
	if {[::file exists $f]} {return $cx/$tl}
	set f $c/Attic/$tl,v
	if {[::file exists $f]} {return $cx/$tl}
    }

    puts stderr <$f>
    return -code error "Unable to locate actual file for $path"
}

proc ::cvs::rcsfile {path} {
    variable rpaths
    if {![info exists rpaths($path)]} {
	set rpaths($path) [RcsFile $path]
    }
    return $rpaths($path)
}

proc ::cvs::RcsFile {path} {
    variable base

    set f $base/$path,v
    if {[::file exists $f]} {return $f}

    set hd [::file dirname $path]
    set tl [::file tail    $path]

    set f $base/$hd/Attic/$tl,v
    if {[::file exists $f]} {return $f}

    # We do not have truncated directories here, assuming that only
    # norm paths are fed into this command.

    if 0 {
	# Bad. The dir can be truncated, i.e. it may not be an exact
	# subdirectory of base, but deeper inside, with parents
	# between it and base left out. Example (from the tcllib
	# history file):
	#
	# M3f1d1245|afaupell|<remote>|ipentry|1.2|ChangeLog The
	# correct path is 'tklib/modules/ipentry'.  This we have to
	# resolve too.

	# normalize dance - old fileutil, modern fileutil (cvs head)
	# doesn't do that.
	set bx [file normalize $base]
	foreach c [fileutil::findByPattern $bx -glob $hd] {
	    set c $base/[fileutil::stripPath $bx $c]

	    set f $c/$tl,v
	    if {[::file exists $f]} {return $f}
	    set f $c/Attic/$tl,v
	    if {[::file exists $f]} {return $f}
	}
    }

    puts stderr <$f>
    return -code error "Unable to locate rcs file for $path"
}

proc ::cvs::history {} {
    variable base
    return $base/CVSROOT/history
}

# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------

cvs::at [lindex $argv 0]

#puts [::cvs::norm ipentry/ChangeLog]
#exit

#changeset state
global cs csf
array set cs {
    start {} end {} cm {}
    usr   {} dt  {}
}
array set csf {}

proc rh {} {
    global argv cs csf repo

    set f [open [cvs::history] r]

    while {[gets $f line] >= 0} {
	# Decode line
	foreach {op usr _ dir rev file} [split [string trim $line] |] break
	set ts [cvs::hextime [string range $op 1 end]]
	set op [string index $op 0]

	# Filter out irrelevant parts
	if {$op eq "O"} continue ; # checkout
	if {$op eq "F"} continue ; # release
	if {$op eq "T"} continue ; # rtag
	if {$op eq "W"} continue ; # delete on update
	if {$op eq "U"} continue ; # update
	if {$op eq "P"} continue ; # update by patch
	#if {$op eq "G"} continue ; # merge on update - FUTURE - identifies mergepoints.
	if {$op eq "C"} continue ; # conflict on update - s.a.
	if {$op eq "E"} continue ; # export
	# left types
	# M: commit
	# A: addition
	# R: removal

	set df $dir/$file
	if {[newcs $op $usr $ts $rev df cause]} {

	    # NOTE 1: ChangeSets containing CVSROOT => remove such files.
	    # NOTE 2: Empty changesets, ignore.

	    #commit
	    csstats

	    if {$cause eq "cmsg"} {
set msg
	    } else {
set msg ""
	    }

	    if {$cs(end) ne ""} {
		puts =============================/$cause\ delta\ [expr {$ts - $cs(end)}]
	    } else {
		puts =============================/$cause
	    }
	    csclear
	}

	# Note: newcs normalizes df, in case the log information is
	# bogus. So the df here may be different from before newcs
	csadd $op $usr $ts $rev $df
	# apply modification to workspace
    }
}

proc newcs {op usr ts rev dfv rv} {
    global cs csf
    upvar 1 $rv reason $dfv df

    # Logic to detect when a new change set begins. A new change sets
    # has started with the current entry when the entry
    #
    # 1. is for a different user than the last.
    # 2. tries to add a file to the changeset which is already part of it.
    # 3.is on the trunk, and the last on a branch, or vice versa.
    # 4. the current entry has a different commit message than the last.

    set df [cvs::norm $df]

    # User changed
    if {$usr ne $cs(usr)} {
	set reason user
	return 1
    }

    # File is already in the changeset
    if {[info exists csf($df)]} {
	set reason file
	return 1
    }

    # last/current are different regarding trunk/branch
    set depth [llength [split $rev .]]
    if {($cs(lastd) == 2) != ($depth == 2)} {
	set reason branch
	return 1
    }

    # Commit message changed
    if {[cvs::cmsg $cs(lastf) $cs(lastr)] ne [cvs::cmsg $df $rev]} {
	set reason cmsg
	return 1
    }

    # Same changeset
    return 0
}

proc csclear {} {
    global cs csf
    array set cs {start {} usr {} end {} dt {}}
    array unset csf *
    return
}

proc csadd {op usr ts rev df} {
    global cs csf

    if {$cs(usr) eq ""} {set cs(usr) $usr}
    if {$cs(start) eq ""} {
	set cs(start) $ts
    } else {
	lappend cs(dt) [expr {$ts - $cs(end)}]
    }
    set cs(end) $ts

    set csf($df) [list $op $rev]
    set cs(lastf) $df
    set cs(lastr) $rev
    set cs(lastd) [llength [split $rev .]]

    puts [list $op [clock format $ts] $usr $rev $df]
    return
}

proc csstats {} {
    global cs csf

    if {$cs(start) eq ""} return

    puts "files: [array size csf]"
    puts "delta: $cs(dt)"
    puts "range: [expr {$cs(end) - $cs(start)}] seconds"
    return
}

rh

exit

=========================================
new fossil
new fossil workspace

open history

foreach line {
    ignore unwanted lines

    accumulate changesets data
    new change-set => commit and continue

    current branch and branch of new change different ?
    => move fossil workspace to proper revision.

    apply change to workspace
    uncommitted
}

if uncommitted => commit
delete workspace
copy fossil repo to destination
=========================================

Not dealt with in outline: branches, tags, merging

=========================================

complexities
- apply to workspace
  - remove simple, direct translation
  - add => requires extraction of indicated revision from ,v
  - modify => see above (without add following)

- ,v file => Can be the either dir/file,v, or dir/Attic/file,v
  Both ? Priority ?

- How to detect changes on branches ?

- Have to keep knowledge of which branches went there.
 => save change-sets information, + uuid in fossil
 => need only the leaves of each branch, and of branch points.
 => better keep all until complete.
 => uuid can be gotten from 'manifest.uuid' in workspace.
- keep tag information ? (symbolics)

=========================================

CVSROOT=ORIGIN

cvs -d ORIGIN checkout -r REV FILE
Extract specific revision of a specific file.
-q, -Q quietness
