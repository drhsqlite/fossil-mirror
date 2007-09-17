# -----------------------------------------------------------------------------
# Repository management (FOSSIL)

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log  ; # User feedback
package require vc::fossil::cmd ; # Access to fossil application.

namespace eval ::vc::fossil::ws {
    vc::tools::log::system fossil
    namespace import ::vc::tools::log::write
    namespace import ::vc::fossil::cmd::do
    namespace import ::vc::fossil::cmd::dova
}

# -----------------------------------------------------------------------------
# API

# vc::fossil::ws::configure key value         - Configure the subsystem.
# vc::fossil::ws::begin     src               - Start new workspace for directory
# vc::fossil::ws::done      dst               - Close workspace and copy to destination.
# vc::fossil::ws::commit    cset usr time msg - Look for changes and commit as new revision.

# Configuration keys:
#
# -nosign  bool		default 0 (= sign imported changesets)
# -breakat num		default empty, no breakpoint.
#			Otherwise stop before committing the identified changeset.
# -saveto  path		default empty, no saving.
#			Otherwise save the commit command to a file.
# -appname string	Default empty. Text to add to all commit messages.
# -ignore  cmdprefix	Command to check if a file is relevant to the commit or not.
#			Signature: cmdprefix path -> bool; true => ignore.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::fossil::ws::configure {key value} {
    variable nosign
    variable breakat
    variable saveto
    variable appname
    variable ignore

    switch -exact -- $key {
	-appname { set appname $value }
	-breakat { set breakat $value }
	-ignore  { set ignore  $value }
	-nosign {
	    if {![string is boolean -strict $value]} {
		return -code error "Expected boolean, got \"$value\""
	    }
	    set nosign $value
	}
	-saveto  { set saveto $value }
	default {
	    return -code error "Unknown switch $key, expected one of \
                                   -appname, -breakat, -ignore, -nosign, or -saveto"
	}
    }
    return
}

proc ::vc::fossil::ws::begin {origin} {
    variable base [file normalize $origin]
    variable rp   [file normalize [fileutil::tempfile import2_fsl_rp_]]

    cd $origin

    dova new  $rp ; # create and ...
    dova open $rp ; # ... connect

    write 0 fossil "Repository: $rp"
    return
}

proc ::vc::fossil::ws::done {destination} {
    variable rp
    file rename -force $rp $destination
    set rp {}
    return
}

proc ::vc::fossil::ws::commit {cset user timestamp message} {
    variable lastuuid
    variable base

    cd $base

    # Commit the current state of the workspace. Scan for new and
    # removed files and issue the appropriate fossil add/rm commands
    # before actually comitting.

    HandleChanges added removed changed

    # Now commit, using the provided meta data, and capture the uuid
    # of the new baseline.

    set cmd [Command $cset [Message $user $timestamp $message]]

    if {[catch {
	do $cmd
    } line]} {
	if {![string match "*nothing has changed*" $line]} {
	    return -code error $line
	}

	# 'Nothing changed' can happen for changesets containing only
	# dead-first revisions of one or more files. For fossil we
	# re-use the last baseline. TODO: Mark them as branchpoint,
	# and for what file.

	write 1 fossil "UNCHANGED, keeping last"

	return [list $lastuuid 0 0 0]
    }

    # Extract the uuid of the new revision.
    regsub -nocase -- {^\s*New_Version:\s*} [string trim $line] {} uuid

    set lastuuid $uuid
    return [list $uuid $added $removed $changed]
}

# -----------------------------------------------------------------------------
# Internal helper commands, and data structures.

proc ::vc::fossil::ws::HandleChanges {av rv cv} {
    upvar 1 $av added $rv removed $cv changed

    set added   0
    set removed 0
    set changed 0

    # Look for modified/removed files first, that way there won't be
    # any ADDED indicators. Nor REMOVED, only EDITED. Removed files
    # show up as EDITED while they are not registered as removed.

    foreach line [split [do changes] \n] {
        regsub {^\s*EDITED\s*} $line {} path
        if {[Ignore $path]} continue

        if {![file exists $path]} {
	    dova rm $path
            incr removed
            write 2 fossil "-  $path"
        } else {
            incr changed
            write 2 fossil "*  $path"
        }
    }

    # Now look for unregistered added files.

    foreach path [split [do extra] \n] {
        if {[Ignore $path]} continue
        dova add $path
        incr added
        write 2 fossil "+  $path"
    }

    return
}

proc ::vc::fossil::ws::Message {user timestamp message} {
    variable appname
    set lines {}
    lappend lines "-- Originally by $user @ $timestamp"
    if {$appname ne ""} {
	lappend lines "-- Imported by $appname"
    }
    lappend lines [string trim $message]
    return [join $lines \n]
}

proc ::vc::fossil::ws::Command {cset message} {
    variable nosign
    variable saveto
    variable breakat

    set cmd [list commit -m $message]

    if {$nosign}           { lappend cmd --nosign }
    if {$saveto ne ""}     { fileutil::writeFile $saveto "$cmd\n" }

    if {$breakat eq $cset} {
	write 0 fossil Stopped.
	exit 0
    }

    return $cmd
}

proc ::vc::fossil::ws::Ignore {path} {
    variable ignore
    if {![llength $ignore]} {return 0}
    return [uplevel #0 [linsert $ignore end $path]]
}

namespace eval ::vc::fossil::ws {
    # Configuration settings.
    variable nosign 0   ; # Sign imported changesets
    variable breakat {} ; # Do not stop
    variable saveto  {} ; # Do not save commit message
    variable appname {} ; # Name of importer application using the package.
    variable ignore  {} ; # No files to ignore.

    variable base     {} ; # Workspace directory
    variable rp       {} ; # Repository the package works on.
    variable lastuuid {} ; # Uuid of last imported changeset.

    namespace export configure begin done commit
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::ws 1.0
return
