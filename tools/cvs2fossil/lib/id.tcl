# # ## ### ##### ######## #############

## A simple class for handling an in-memory index mapping from
## arbitrary strings to a small numeric id. Can be queried in reverse
## too, returning the string for the id.

## Id's are starting from 1.

# # ## ### ##### ######## #############
## Requirements.

package require Tcl  ; # Runtime.
package require snit ; # OO runtime.

# # ## ### ##### ######## #############
## Implementation.

snit::type ::vc::tools::id {
    # # ## ### ##### ######## #############

    constructor {} {}

    # # ## ### ##### ######## #############
    ## Public API.
    ## - Put data into the index, incl. query for id of key.
    ## - Lookup data for id.

    method put {key} {
	if {[info exists mydata($key)]} { return $mydata($key) }
	incr mycounter

	set mydata($key)   $mycounter
	set myinvert($mycounter) $key

	return $mycounter
    }

    # Explicitly load the database with a mapping.
    method map {id key} {
	set mydata($key)   $id
	set myinvert($id) $key
    }

    method keyof {id} { return $myinvert($id) }
    method get   {}   { return [array get mydata] }

    # # ## ### ##### ######## #############
    ## Internal. State.

    variable mydata   -array {} ; # Map data -> id
    variable myinvert -array {} ; # Map id -> data
    variable mycounter        0 ; # Counter for id generation.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools {
    namespace export id
}

# # ## ### ##### ######## #############
## Ready.

package provide vc::tools::id 1.0
