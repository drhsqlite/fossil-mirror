# -----------------------------------------------------------------------------
# Repository management (FOSSIL)

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4

namespace eval ::fossil {}

# -----------------------------------------------------------------------------
# API

# Define repository file, and connect to workspace in CWD.

proc ::fossil::new {} {
    variable fr     [file normalize [fileutil::tempfile import2_fsl_rp_]]
    variable fossil

    # pwd = workspace
    exec $fossil new  $fr ; # create and
    exec $fossil open $fr ; # connect

    Log info "    Fossil:    $fr"

    return $fr
}

# Define logging callback command

proc ::fossil::feedback {logcmd} {
    variable lc $logcmd
    return
}

# Move generated fossil repository to final destination

proc ::fossil::destination {path} {
    variable fr
    file rename $fr $path
    return
}

namespace eval ::fossil {
    # Repository file
    variable fr {}

    # Fossil application
    variable fossil [auto_execok fossil]
}


proc ::fossil::commit {appname nosign meta ignore} {
    variable fossil
    variable lastuuid

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
	    Log info "        ** - $path"
	} else {
	    incr changed
	    Log info "        ** * $path"
	}
    }

    # Now look for unregistered added files.

    foreach path [split [exec $fossil extra] \n] {
	if {[IGNORE $ignore $path]} continue
	exec $fossil add $path
	incr added
	Log info "        ** + $path"
    }

    # Now commit, using the provided meta data, and capture the uuid
    # of the new baseline.

    foreach {user message tstamp} $meta break

    set message [join [list \
			   "-- Originally by $user @ $tstamp" \
			   "-- Imported by $appname" \
			   $message] \n]

    if {[catch {
	if {$nosign} {
	    exec $fossil commit -m $message --nosign
	} else {
	    exec $fossil commit -m $message
	}
    } line]} {
	if {![string match "*nothing has changed*" $line]} {
	    return -code error $line
	}

	# 'Nothing changed' can happen for changesets containing only
	# dead-first revisions of one or more files. For fossil we
	# re-use the last baseline. TODO: Mark them as branchpoint,
	# and for what file.

	Log info "        UNCHANGED, keeping last"

	return [list $lastuuid 0 0 0]
    }

    set line [string trim $line]
    regsub -nocase -- {^\s*New_Version:\s*} $line {} uuid

    set lastuuid $uuid
    return [list $uuid $added $removed $changed]
}

# -----------------------------------------------------------------------------
# Internal helper commands

proc ::fossil::IGNORE {ignore path} {
    return [uplevel #0 [linsert $ignore end $path]]
}

proc ::fossil::Log {level text} {
    variable lc
    uplevel #0 [linsert $lc end $level $text]
    return
}

proc ::fossil::Nop {args} {}

namespace eval ::fossil {
    # Logging callback. No logging by default.
    variable lc ::fossil::Nop
}

# -----------------------------------------------------------------------------
# Ready

package provide fossil 1.0
return
