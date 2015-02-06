## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2007-2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Trunk, the special main line of development in a project.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::project::trunk {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {project} {
	set mysymbol [$project getsymbol $myname]
	set myid     [$mysymbol id]
	return
    }

    destructor {
	$mysymbol destroy
    }

    method name    {} { return $myname }
    method id      {} { return $myid   }
    method istrunk {} { return 1 }
    method symbol  {} { return $self }
    method parent  {} { return $self }

    method forceid {id} { set myid $id ; return }

    method defcounts {tc bc cc} {}

    method countasbranch {} {}
    method countastag    {} {}
    method countacommit  {} {}

    method blockedby      {symbol} {}
    method possibleparent {symbol} {}

    method isghost {} { return 0 }

    delegate method persistrev to mysymbol

    method determinetype {} { $mysymbol markthetrunk }

    # # ## ### ##### ######## #############
    ## State

    typevariable myname   :trunk: ; # Name shared by all trunk symbols.
    variable     myid     {}      ; # The trunk's symbol id.
    variable     mysymbol {}      ; # The symbol underneath the trunk.

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::project {
    namespace export trunk
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::trunk 1.0
return
