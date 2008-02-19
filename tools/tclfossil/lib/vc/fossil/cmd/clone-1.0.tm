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


# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::fossil::cmd 1.0                 ; # Subcommand management
package require vc::fossil::blob 1.0
                
package provide vc::fossil::cmd::clone 1.0

# # ## ### ##### ######## ############# #####################
## Imports

namespace import ::vc::fossil::blob


# # ## ### ##### ######## ############# #####################
##

vc::fossil::cmd add clone

namespace eval ::vc::fossil::cmd {
    proc clone {args} {
	if {[ui argc] != 4} {
	    ui usage "FILE-OR-URL NEW-REPOSITORY"
	}
	
	set local_file [lindex [ui argv] 3]
	if {[file exists $local_file]} {
	    ui panic "file already exists: $local_file"
	}
	puts "cloning: $args"
	package require http
	package require sha1
	package require autoproxy

	autoproxy::init
	puts [autoproxy::configure]

	proc login_card {userid password message} {
	    # calculates the login card for the specific user for this msg

	    set nonce [sha1::sha1 -hex $message]
	    set signature [sha1::sha1 -hex $nonce$password]
	    return "login $userid $nonce $signature\n"
	}

	proc http_req {url user password message} {
	    set login_card [login_card $user $password $message]
	    blob blob_a -data $login_card$message
	    set message [blob_a compress]
	    blob_a destroy
	    return [http::geturl $url/xfer -binary 1 -query $message -type application/x-fossil]
	}


	set tok [http_req http://www.fossil-scm.org/fossil MJanssen {} clone\n]
	http::wait $tok
	set zip_body  [http::data $tok]
	blob blob_a -data $zip_body
	set body [blob_a decompress]
	blob_a destroy
	set lines [split $body \n] 
	puts $body
	puts "Received:\t[string length $body] ([string length $zip_body]) bytes,\t[llength $lines] messages"


    }
}