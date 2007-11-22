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

## Utility package, export graph data to dot format for formatting
## with neato et. all

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4  ; # Required runtime
package require snit     ; # OO system.
package require fileutil ; # Helper commands.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::tools::dot {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    typemethod format {g name} {
	lappend lines "digraph \"$name\" \{"

	foreach n [$g nodes] {
	    set cmd "\"$n\""
	    set sep " "
	    set head " \["
	    set tail ""
	    foreach {gattr nodekey} {
		label label
		shape shape
	    } {
		if {![$g node keyexists $n $nodekey]} continue
		append cmd "$head$sep${gattr}=\"[$g node get $n $nodekey]\""

		set sep ", "
		set head ""
		set tail " \]"
	    }

	    append cmd ${tail} ";"
	    lappend lines $cmd
	}
	foreach a [$g arcs] {
	    lappend lines "\"[$g arc source $a]\" -> \"[$g arc target $a]\";"
	}

	lappend lines "\}"
	return [join $lines \n]
    }

    typemethod write {g name file} {
	fileutil::writeFile $file [$type format $g $name]
	return
    }

    typemethod layout {format g name file} {
	set f [fileutil::tempfile c2fdot_]
	$type write $g $name $f
	exec dot -T $format -o $file $f
	file delete $f
	return
    }

    # # ## ### ##### ######## #############
    ## Internal, state

    # # ## ### ##### ######## #############
    ## Internal, helper methods (formatting, dispatch)

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools {
    namespace export dot
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::dot 1.0
return
