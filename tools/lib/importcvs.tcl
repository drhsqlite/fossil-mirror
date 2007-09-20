# -----------------------------------------------------------------------------
# Tool packages. Main control module for importing from a CVS repository.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::cvs::ws               ; # Frontend, reading from source repository
package require vc::fossil::ws            ; # Backend,  writing to destination repository.
package require vc::tools::log            ; # User feedback.
package require vc::fossil::import::stats ; # Management for the Import Statistics.
package require vc::fossil::import::map   ; # Management of the cset <-> uuid mapping.

namespace eval ::vc::fossil::import::cvs {
    vc::tools::log::system import
    namespace import ::vc::tools::log::write
    namespace eval cvs    { namespace import ::vc::cvs::ws::* }
    namespace eval fossil { namespace import ::vc::fossil::ws::* }
    namespace eval stats  { namespace import ::vc::fossil::import::stats::* }
    namespace eval map    { namespace import ::vc::fossil::import::map::* }

    fossil::configure -appname cvs2fossil
    fossil::configure -ignore  ::vc::cvs::ws::isadmin
}

# -----------------------------------------------------------------------------
# API

# Configuration
#
#	vc::fossil::import::cvs::configure key value - Set configuration
#
#       Legal keys:     -nosign  <bool>, default false
#                       -breakat <int>,  default :none:
#                       -saveto  <path>, default :none:
#                       -limit   <path>, default :none:
#
# Functionality
#
#	vc::fossil::import::cvs::run src dst         - Perform an import.

# -----------------------------------------------------------------------------
# API Implementation - Functionality

proc ::vc::fossil::import::cvs::configure {key value} {
    # The options are simply passed through to the fossil importer
    # backend.
    switch -exact -- $key {
	-breakat { fossil::configure -breakat $value }
	-nosign  { fossil::configure -nosign  $value }
	-project { cvs::configure    -project $value }
	-saveto  { fossil::configure -saveto  $value }
	default {
	    return -code error "Unknown switch $key, expected one of \
                                   -breakat, -nosign, or -saveto"
	}
    }
    return
}

# Import the CVS repository found at directory 'src' into the new
# fossil repository at 'dst'.

proc ::vc::fossil::import::cvs::run {src dst} {
    map::set {} {}

    set src [file normalize $src]
    set dst [file normalize $dst]

    set ws [cvs::begin $src]
    fossil::begin $ws
    stats::setup [cvs::nimportable] [cvs::ncsets]

    cvs::foreach cset {
	Import1 $cset
    }

    stats::done
    fossil::done $dst
    cvs::done

    write 0 import Ok.
    return
}

# -----------------------------------------------------------------------------
# Internal operations - Import a single changeset.

proc ::vc::fossil::import::cvs::Import1 {cset} {
    stats::csbegin $cset

    set microseconds [lindex [time {ImportCS $cset} 1] 0]
    set seconds      [expr {$microseconds/1e6}]

    stats::csend $seconds
    return
}

proc ::vc::fossil::import::cvs::ImportCS {cset} {
    fossil::setup [map::get [cvs::parentOf $cset]]
    lassign [cvs::checkout  $cset] user  timestamp  message
    lassign [fossil::commit $cset $user $timestamp $message] uuid ad rm ch
    write 2 import "== +${ad}-${rm}*${ch}"
    map::set $cset $uuid
    return
}

proc ::vc::fossil::import::cvs::lassign {l args} {
    foreach v $args {upvar 1 $v $v} 
    foreach $args $l break
    return
}

# -----------------------------------------------------------------------------

namespace eval ::vc::fossil::import::cvs {
    namespace export run configure
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::import::cvs 1.0
return
