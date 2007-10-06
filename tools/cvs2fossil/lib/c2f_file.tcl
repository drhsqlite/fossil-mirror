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

package require Tcl 8.4                          ; # Required runtime.
package require snit                             ; # OO system.

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

    method begin {} {}
    method sethead {h} {}
    method setprincipalbranch {b} {}
    method setsymbols {dict} {}
    method setcomment {c} {}
    method admindone {} {}
    method def {rev date author state next branches} {}
    method setdesc {d} {}
    method extend {rev commitmsg deltarange} {}
    method done {} {}

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
    ## State

    variable mypath    {} ; # Path of rcs archive
    variable myproject {} ; # Project object the file belongs to.

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
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file 1.0
return
