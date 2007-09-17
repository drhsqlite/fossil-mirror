# -----------------------------------------------------------------------------
# Repository management (CVS), archive files

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require fileutil::traverse ; # Tcllib (traverse directory hierarchy)
package require vc::tools::log     ; # User feedback

namespace eval ::vc::cvs::ws::files {
    namespace import ::vc::tools::log::write
    namespace import ::vc::tools::log::progress
}

# -----------------------------------------------------------------------------
# API

# vc::cvs::ws::files::find path - Find all RCS archives under the path.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::cvs::ws::files::find {path} {

    write 0 cvs "Scanning directory hierarchy $path ..."

    set t [fileutil::traverse %AUTO% $path]
    set n 0
    set r {}

    $t foreach rcs {
	if {![string match *,v $rcs]} continue

	# Now make rcs is relative to the base/project
	set rcs [fileutil::stripPath $path $rcs]

	if {[string match CVSROOT/* $rcs]} {
	    write 2 cvs "Ignoring administrative file: $rcs"
	    continue
	}

	set f [UserFile $rcs isattic]

	if {$isattic && [file exists $path/$f,v]} {
	    # We have a regular archive and an Attic archive refering
	    # to the same user visible file. Ignore the file in the
	    # Attic.

	    write 2 cvs "Ignoring superceded attic:    $rcs"

	    # TODO/CHECK. My method of co'ing exact file revisions per
	    # the info in the collected csets has the flaw that I may
	    # have to know exactly when what archive file to use, see
	    # above. It might be better to use the info only to gather
	    # when csets begin and end, and then to co complete slices
	    # per exact timestamp (-D) instead of file revisions
	    # (-r). The flaw in that is that csets can occur in the
	    # same second (trf, memchan - check for examples). For
	    # that exact checkout may be needed to recreate exact
	    # sequence of changes. Grr. Six of one ...

	    continue
	}

	lappend r $rcs $f
	incr n
	progress 0 cvs $n {}
    }

    $t destroy
    return $r
}

# -----------------------------------------------------------------------------
# Internals

proc ::vc::cvs::ws::files::UserFile {rcs iav} {
    upvar 1 $iav isattic

    # Derive the regular path from the rcs path. Meaning: Chop of the
    # ",v" suffix, and remove a possible "Attic".

    set f [string range $rcs 0 end-2]

    if {"Attic" eq [lindex [file split $rcs] end-1]} {

	# The construction below ensures that Attic/X maps to X
	# instead of ./X. Otherwise, Y/Attic/X maps to Y/X.

	set fx [file dirname [file dirname $f]]
	set f  [file tail $f]
	if {$fx ne "."} { set f [file join $fx $f] }

	set isattic 1
    } else {
	set isattic 0
    }

    return $f
}

# -----------------------------------------------------------------------------

namespace eval ::vc::cvs::ws::files {
    namespace export find
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::cvs::ws::files 1.0
return
