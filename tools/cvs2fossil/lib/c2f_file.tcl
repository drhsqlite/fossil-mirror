## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## File, part of a project, part of a CVS repository. Multiple
## instances are possible.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::fossil::import::cvs::repository ; # Repository management.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {path project} {
	set mypath    $path
	set myproject $project
	return
    }

    method path {} { return $mypath }

    # # ## ### ##### ######## #############
    ## Methods required for the class to be a sink of the rcs parser

    method begin     {} {}
    method done      {} {}
    method admindone {} {}

    method sethead            {h} {}
    method setprincipalbranch {b} {}

    method setsymbols {dict} {}
    method setcomment {c}    {}
    method setdesc    {d}    {}

    method def    {rev date author state next branches} {}
    method extend {rev commitmsg deltarange} {}

    #method begin {} {puts begin}
    #method sethead {h} {puts head=$h}
    #method setprincipalbranch {b} {puts pb=$b}
    #method setsymbols {dict} {puts symbols=$dict}
    #method setcomment {c} {puts comment=$c}
    #method admindone {} {puts admindone}
    #method def {rev date author state next branches} {puts "def $rev $date $author $state $next $branches"}
    #method setdesc {d} {puts desc=$d}
    #method extend {rev commitmsg deltarange} {puts "extend $commitmsg $deltarange"}
    #method done {} {puts done}

    # # ## ### ##### ######## #############
    ## Persistence (pass II)

    method persist {} {
    }

    # # ## ### ##### ######## #############
    ## Implement the sink

    method begin     {} {}
    method done      {} {}
    method admindone {} {}

    method sethead {h} {
	set myhead $h
	return
    }

    method setprincipalbranch {b} {
	set myprincipal $b
	return
    }

    method setsymbols {dict} {
	# Slice symbols into branches and tags, canonical numbers ...
array set _ $dict
parray _
    }

    method setcomment {c} {# ignore}
    method setdesc    {d} {# ignore}

    method def {rev date author state next branches} {
	set myrev($rev) [list $date $author $state $next $branches]
	repository author $author
	return
    }

    method extend {rev commitmsg deltarange} {
	set cm [string trim $commitmsg]
	lappend myrev($rev) $cm $deltarange
	repository cmessage $cm
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable mypath       {} ; # Path of rcs archive
    variable myproject    {} ; # Project object the file belongs to.
    variable myrev -array {} ; # All revisions and their connections.
    variable myhead       {} ; # Head revision    (rev number)
    variable myprincipal  {} ; # Principal branch (branch number)

    # # ## ### ##### ######## #############
    ## Internal methods

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export file
    namespace eval file {
	namespace import ::vc::fossil::import::cvs::repository
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file 1.0
return
