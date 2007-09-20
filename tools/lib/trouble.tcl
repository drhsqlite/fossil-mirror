# -----------------------------------------------------------------------------
# Tool packages. Error reporting.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log

namespace eval ::vc::tools::trouble {
    ::vc::tools::log::system trouble
    namespace import ::vc::tools::log::write
}

# -----------------------------------------------------------------------------
# API

# vc::tools::trouble::add message - Report error (shown in general
#                                   log), and remember for re-display at exit.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::tools::trouble::add {text} {
    variable messages
    lappend  messages $text
    write trouble 0   $text
    return
}

# -----------------------------------------------------------------------------
# Internals. Hook into the application exit, show the remembered messages, then
# pass through the regular command.

rename ::exit vc::tools::trouble::EXIT
proc   ::exit {{status 0}} {
    variable ::vc::tools::trouble::messages
    foreach m $messages {
	write trouble 0 $m
    }
    ::vc::tools::trouble::EXIT $status
    # Not reached.
    return
}

namespace eval ::vc::tools::trouble {
    # List of the remembered error messages to be shown at exit
    variable messages {}

    namespace export add 
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::trouble 1.0
return
