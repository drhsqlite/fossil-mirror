# -----------------------------------------------------------------------------
# Access to the external cvs command.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
namespace eval ::vc::cvs::cmd {}

# -----------------------------------------------------------------------------
# API

# vc::cvs::cmd::dova word... - Run a cvs command specified as var args.
# vc::cvs::cmd::do   words   - Run a cvs command specified as a list.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::cvs::cmd::dova {args} {do $args}

proc ::vc::cvs::cmd::do {words} {
    variable cmd
    if {![llength $words]} {
	return -code error "Empty cvs command"
    }
    # 8.5: exec $cmd {*}$words
    return [eval [linsert $words 0 exec $cmd]]
}

# -----------------------------------------------------------------------------
# Internals.

namespace eval ::vc::cvs::cmd {
    # Locate external cvs application.
    variable cmd [auto_execok cvs]

    # Bail out if not found.
    if {![llength $::vc::cvs::cmd::cmd]} {
	return -code error "Cvs application not found."
    }

    namespace export do dova
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::cvs::cmd 1.0
return
