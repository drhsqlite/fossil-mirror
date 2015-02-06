## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Andreas Kupries.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Blob storage. Each instance stores the blob data of a single rcs
## archive file, i.e. which file, all text ranges, delta dependencies,
## and associated revisions (as object references). The data is
## persistent and used by the import pass(es) to expand the revisions
## of a file.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::fossil::import::cvs::state      ; # State storage.
package require vc::fossil::import::cvs::integrity  ; # State integrity checks.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::log                      ; # User feedback
#package require vc::tools::misc                     ; # Text formatting

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::blobstore {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {fid} {
	set       myfile $fid
	array set myparent {}
	array set myblob   {}
	return
    }

    method setid {id} {
	integrity assert {$myfile eq ""} {Already has an id, '$myfile'}
	set myfile $id
	return
    }

    # Remember the file revision object for the revision REVNR.

    method add {revnr rev} {
	set myblob($revnr) $rev
	return
    }

    # Remember that the DELTA revision is specified as a delta against
    # the BASE revision. Both are specified as revision numbers.

    method delta {delta base} {
	set myparent($delta) $base
	return
    }

    # Specify the text range in the archive file for the data of the
    # revision identified by REVNR.

    method extend {revnr textrange} {
	struct::list assign $textrange coff end
	set clen [expr {$end - $coff}]
	lappend myblob($revnr) $coff $clen
	return
    }

    # Write the stored information into the persistent state.

    method persist {} {
	array set bids {}
	state transaction {
	    # Phase I: Store the basic blob information.

	    foreach revnr [lsort [array names myblob]] {
		struct::list assign $myblob($revnr) rev coff clen
		state run {
		    INSERT INTO blob (bid, rid,   fid,     coff,  clen,  pid)
		    VALUES           (NULL, NULL, $myfile, $coff, $clen, NULL)
		}
		set current [state id]
		set bids($revnr) $current

		# Ia. Set the reference to the revision of the blob,
		# if applicable. We can have blobs without revisions,
		# their revisions were removed as irrelevant. We need
		# them however for the proper delta ordering and patch
		# application when expanding a file (-> Import passes).

		set rid [$rev id]
		if {$rid eq ""} continue
		state run {
		    UPDATE blob
		    SET    rid = $rid
		    WHERE  bid = $current
		}
	    }

	    # Phase II: Set the parent links for deltas.
	    foreach revnr [array names myparent] {
		set bid $bids($revnr)
		set pid $bids($myparent($revnr))

		state run {
		    UPDATE blob
		    SET    pid = $pid
		    WHERE  bid = $bid
		}
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myfile          {} ; # Id of the file the blobs belong to.
    variable myparent -array {} ; # Map delta-encoded revision numbers
				  # to their baseline revisions.
    variable myblob   -array {} ; # Map revision numbers to associated
				  # file revision object and text
				  # range.

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export blobstore
    namespace eval blobstore {
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::blobstore 1.0
return
