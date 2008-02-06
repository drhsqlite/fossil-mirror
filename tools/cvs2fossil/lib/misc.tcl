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

## Utilities for various things: text formatting, max, ...

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4 ; # Required runtime

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::tools::misc {
    # # ## ### ##### ######## #############
    ## Public API, Methods

    # Choose singular vs plural forms of a word based on a number.

    proc sp {n singular {plural {}}} {
	if {$n == 1} {return $singular}
	if {$plural eq ""} {set plural ${singular}s}
	return $plural
    }

    # As above, with the number automatically put in front of the
    # string.

    proc nsp {n singular {plural {}}} {
	return "$n [sp $n $singular $plural]"
    }

    # Find maximum/minimum in a list.

    proc max {list} {
	set max -1
	foreach e $list {
	    if {$e < $max} continue
	    set max $e
	}
	return $max
    }

    proc min {list} {
	set min {}
	foreach e $list {
	    if {$min == {}} {
		set min $e
	    } elseif {$e > $min} continue
	    set min $e
	}
	return $min
    }

    proc max2 {a b} {
	if {$a > $b}  { return $a }
	return $b
    }

    proc min2 {a b} {
	if {$a < $b}  { return $a }
	return $b
    }

    proc ldelete {lv item} {
	upvar 1 $lv list
	set pos [lsearch -exact $list $item]
	if {$pos < 0} return
	set list [lreplace $list $pos $pos]
	return
    }

    # Delete item from list by name

    proc striptrailingslash {path} {
	# split and rejoin gets rid of a traling / character.
	return [eval [linsert [file split $path] 0 ::file join]]
    }

    # The windows filesystem is storing file-names case-sensitive, but
    # matching is case-insensitive. That is a problem as without
    # precaution the two files Attic/X,v and x,v may be mistakenly
    # identified as the same file. A similar thing can happen for
    # files and directories. To prevent such mistakes we need commands
    # which do case-sensitive file matching even on systems which do
    # not perform this natively. These are below.

    if {$tcl_platform(platform) eq "windows"} {
	# We use glob to get the list of files (with proper case in
	# the names) to perform our own, case-sensitive matching. WE
	# use 8.5 features where possible, for clarity.

	if {[package vsatisfies [package present Tcl] 8.5]} {
	    proc fileexists_cs {path} {
		set dir  [::file dirname $path]
		set file [::file tail    $path]
		return [expr {$file in [glob -nocomplain -tail -directory $dir *]}]
	    }

	    proc fileisdir_cs {path} {
		set dir  [::file dirname $path]
		set file [::file tail    $path]
		return [expr {$file in [glob -nocomplain -types d -tail -directory $dir *]}]
	    }
	} else {
	    proc fileexists_cs {path} {
		set dir  [::file dirname $path]
		set file [::file tail    $path]
		return [expr {[lsearch [glob -nocomplain -tail -directory $dir *] $file] >= 0}]
	    }

	    proc fileisdir_cs {path} {
		set dir  [::file dirname $path]
		set file [::file tail    $path]
		return [expr {[lsearch [glob -nocomplain -types d -tail -directory $dir *] $file] >= 0}]
	    }
	}
    } else {
	proc fileexists_cs {path} { return [file exists      $path] }
	proc fileisdir_cs  {path} { return [file isdirectory $path] }
    }

    # # ## ### ##### ######## #############
}

namespace eval ::vc::tools::misc {
    namespace export sp nsp max min max2 min2 ldelete
    namespace export striptrailingslash fileexists_cs fileisdir_cs
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::misc 1.0
return
