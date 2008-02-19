## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Mark Janssen.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Commands for creating and managing fossil blobs.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require sqlite3                             ; # Fossil database access
package require snit                                ; # OO system.
package require zlib

package provide vc::fossil::blob 1.0

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::fossil {
    namespace export blob
    snit::type blob {
	option -data ""

	constructor {args} {
	    $self configurelist $args
	}

	method compress {} {
	    set data [$self cget -data]
	    set n [string length $data]
	    set data [zlib compress $data 9]
	    set header [binary format I $n]
	    return $header$data
	}

	method  decompress {} {
	    set data [$self cget -data]
	    binary scan $data I length
	    return [zlib decompress [string range $data 4 end] $length ]
	} 
    }
}

