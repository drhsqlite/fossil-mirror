# -----------------------------------------------------------------------------
# Tool packages. Main control module for importing from a CVS repository.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::cvs::ws               ; # Frontend, reading from source repository
package require vc::fossil::ws            ; # Backend,  writing to destination repository.
package require vc::tools::log            ; # User feedback
package require vc::fossil::import::stats ; # Import Statistics

namespace eval ::vc::fossil::import::cvs {
    vc::tools::log::system import
    namespace import ::vc::tools::log::write
    namespace eval cvs    { namespace import ::vc::cvs::ws::* }
    namespace eval fossil { namespace import ::vc::fossil::ws::* }
    namespace eval stats  { namespace import ::vc::fossil::import::stats::* }

    fossil::configure -appname cvs2fossil
    fossil::configure -ignore  ::vc::cvs::ws::wsignore
}

# -----------------------------------------------------------------------------
# API

# Configuration
#
#	vc::fossil::import::cvs::configure key value - Set configuration
#
#	Legal keys:	-nosign		<bool>, default false
#			-breakat	<int>,  default :none:
#			-saveto		<path>, default :none:
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
    cvs::at $src  ; # Define location of CVS repository
    cvs::scan     ; # Gather revision data from the archives
    cvs::csets    ; # Group changes into sets
    cvs::rtree    ; # Build revision tree (trunk only right now).

    write 0 import {Begin conversion}
    write 0 import {Setting up workspaces}

    cvs::workspace      ; # cd's to workspace
    fossil::begin [pwd] ; # Uses cwd as workspace to connect to.
    stats::setup [cvs::ntrunk] [cvs::ncsets]

    cvs::foreach_cset cset [cvs::root] {
	OneChangeSet $cset
    }

    stats::done
    cvs::wsclear
    fossil::close $dst

    write 0 import Ok.
    return
}

# -----------------------------------------------------------------------------
# Internal operations - Import a single changeset.

proc ::vc::fossil::import::cvs::OneChangeSet {cset} {
    stats::csbegin $cset

    set microseconds [lindex [time {
	foreach {user message timestamp} [cvs::wssetup $cset] break
	foreach {uuid ad rm ch} [fossil::commit $cset $user $timestamp $message] break
    } 1] 0]
    set seconds [expr {$microseconds/1e6}]

    cvs::uuid $cset $uuid
    write 2 import "== $uuid +${ad}-${rm}*${ch}"

    stats::csend $seconds
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
