# -----------------------------------------------------------------------------
# Access to the external fossil command.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
namespace eval ::vc::fossil::cmd {}

# -----------------------------------------------------------------------------
# API

# vc::fossil::cmd::dova word... - Run a fossil command specified as var args
# vc::fossil::cmd::do   words   - Run a fossil command specified in a list.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::fossil::cmd::dova {args} {do $args}

proc ::vc::fossil::cmd::do {words} {
    variable cmd
    if {![llength $words]} {
	return -code error "Empty fossil command"
    }
    # 8.5: exec $cmd {*}$words
    return [eval [linsert $words 0 exec $cmd]]
}

# -----------------------------------------------------------------------------
# Internals.

namespace eval ::vc::fossil::cmd {
    # Locate external fossil application.
    variable cmd [auto_execok fossil]

    # Bail out if not found.
    if {![llength $::vc::fossil::cmd::cmd]} {
	return -code error "Fossil application not found."
    }

    namespace export do dova
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::cmd 1.0
return
