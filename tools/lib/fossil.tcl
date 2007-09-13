# -----------------------------------------------------------------------------
# Repository management (FOSSIL)

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log ; # User feedback

namespace eval ::vc::fossil::ws {
    vc::tools::log::system fossil
    namespace import ::vc::tools::log::write
}

# -----------------------------------------------------------------------------
# API

# Define repository file, and connect to workspace in CWD.

proc ::vc::fossil::ws::new {} {
    variable fr     [file normalize [fileutil::tempfile import2_fsl_rp_]]
    variable fossil

    # pwd = workspace
    exec $fossil new  $fr ; # create and
    exec $fossil open $fr ; # connect

    write 0 fossil "Repository: $fr"

    return $fr
}

# Move generated fossil repository to final destination

proc ::vc::fossil::ws::destination {path} {
    variable fr
    file rename $fr $path
    return
}

namespace eval ::vc::fossil::ws {
    # Repository file
    variable fr {}

    # Fossil application
    variable fossil [auto_execok fossil]

    # Debug the commit command (write a Tcl script containing the
    # exact command used). And the file the data goes to.
    variable debugcommit 0
    variable dcfile      {}
}

proc ::vc::fossil::ws::debugcommit {flag} {
    variable debugcommit $flag
    if {$debugcommit} {
	variable dcfile [file normalize cvs2fossil_commit.tcl]
    }
    return
}

proc ::vc::fossil::ws::commit {break appname nosign meta ignore} {
    variable fossil
    variable lastuuid
    variable debugcommit
    variable dcfile

    # Commit the current state of the workspace. Scan for new and
    # removed files and issue the appropriate fossil add/rm commands
    # before actually comitting.

    # Modified/Removed files first, that way there won't be any ADDED
    # indicators. Nor REMOVED, only EDITED. Removed files show up as
    # EDITED while they are not registered as removed.

    set added   0
    set removed 0
    set changed 0

    foreach line [split [exec $fossil changes] \n] {
	regsub {^\s*EDITED\s*} $line {} path
	if {[IGNORE $ignore $path]} continue

	if {![file exists $path]} {
	    exec $fossil rm $path
	    incr removed
	    write 2 fossil "-  $path"
	} else {
	    incr changed
	    write 2 fossil "*  $path"
	}
    }

    # Now look for unregistered added files.

    foreach path [split [exec $fossil extra] \n] {
	if {[IGNORE $ignore $path]} continue
	exec $fossil add $path
	incr added
	write 2 fossil "+  $path"
    }

    # Now commit, using the provided meta data, and capture the uuid
    # of the new baseline.

    foreach {user message tstamp} $meta break

    set message [join [list \
			   "-- Originally by $user @ $tstamp" \
			   "-- Imported by $appname" \
			   $message] \n]

    if {$nosign} {
	set cmd [list exec $fossil commit -m $message --nosign]
    } else {
	set cmd [list exec $fossil commit -m $message]
    }

    if {$debugcommit} {
	fileutil::writeFile $dcfile "\#!tclsh\n$cmd\n"
    }

    # Stop, do not actually commit.
    if {$break} return

    if {[catch {
	eval $cmd
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

    set line [string trim $line]
    regsub -nocase -- {^\s*New_Version:\s*} $line {} uuid

    set lastuuid $uuid
    return [list $uuid $added $removed $changed]
}

# -----------------------------------------------------------------------------
# Internal helper commands

proc ::vc::fossil::ws::IGNORE {ignore path} {
    return [uplevel #0 [linsert $ignore end $path]]
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::ws 1.0
return
