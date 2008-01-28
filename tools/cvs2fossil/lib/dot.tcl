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

    typemethod format {g name {subgraph {}}} {
	lappend lines "digraph \"$name\" \{"

	if {![llength $subgraph]} {
	    set nodes [$g nodes]
	    set arcs  [$g arcs]
	} else {
	    set nodes $subgraph
	    set arcs [eval [linsert $subgraph 0 $g arcs -inner]]
	}

	foreach n $nodes {
	    set style [Style $g node $n {label label shape shape fontcolor fontcolor}]
	    lappend lines "\"$n\" ${style};"
	}
	foreach a $arcs {
	    set style [Style $g arc $a {color color}]
	    lappend lines "\"[$g arc source $a]\" -> \"[$g arc target $a]\" ${style};"
	}

	lappend lines "\}"
	return [join $lines \n]
    }

    typemethod write {g name file {subgraph {}}} {
	fileutil::writeFile $file [$type format $g $name $subgraph]
	return
    }

    typemethod layout {format g name file} {
	set f [fileutil::tempfile c2fdot_]
	$type write $g $name $f
	exec dot -T $format -o $file $f
	::file delete $f
	return
    }

    # # ## ### ##### ######## #############
    ## Internal, state

    proc Style {graph x y dict} {
	set sep " "
	set head " \["
	set tail ""
	set style ""
	foreach {gattr key} $dict {
	    if {![$graph $x keyexists $y $key]} continue
	    append style "$head$sep${gattr}=\"[$graph $x get $y $key]\""
	    set sep ", "
	    set head ""
	    set tail " \]"
	}

	append style ${tail}
	return $style
    }

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
