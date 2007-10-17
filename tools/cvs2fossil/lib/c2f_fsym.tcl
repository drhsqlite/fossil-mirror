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

## Symbols (Tags, Branches) per file.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::fossil::import::cvs::file::rev  ; # CVS per file revisions.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {symtype nr symbol} {
	set mytype   $symtype
	set mynr     $nr
	set mysymbol $symbol

	switch -exact -- $mytype {
	    branch  { SetupBranch }
	    tag     { }
	    default { trouble internal "Bad symbol type '$mytype'" }
	}
	return
    }

    # Symbol acessor methods.

    delegate method name to mysymbol
    delegate method id   to mysymbol

    # Branch acessor methods.

    method setchildrevnr  {revnr} {
	if {$mybranchchildrevnr ne ""} { trouble internal "Child already defined" }
	set mybranchchildrevnr $revnr
	return
    }

    method setposition {n}   { set mybranchposition $n ; return }
    method setparent   {rev} { set mybranchparent $rev ; return }
    method setchild    {rev} { set mybranchchild  $rev ; return }

    method branchnr    {} { return $mynr }
    method parentrevnr {} { return $mybranchparentrevnr }
    method childrevnr  {} { return $mybranchchildrevnr }
    method haschild    {} { return [expr {$mybranchchildrevnr ne ""}] }
    method parent      {} { return $mybranchparent }
    method child       {} { return $mybranchchild }
    method position    {} { return $mybranchposition }


    # Tag acessor methods.

    method tagrevnr  {}    { return $mynr }
    method settagrev {rev} {set mytagrev $rev ; return }

    # Derived information

    method lod {} { return $mylod }

    method setlod {lod} {
	set mylod $lod
	$self checklod
	return
    }

    method checklod {} {
	# Consistency check. The symbol's line-of-development has to
	# be same as the line-of-development of its source (parent
	# revision of a branch, revision of a tag itself).

	switch -exact -- $mytype {
	    branch  { set slod [$mybranchparent lod] }
	    tag     { set slod [$mytagrev       lod] }
	}

	if {$mylod ne $slod} {
	    trouble fatal "For $mytype [$mysymbol name]: LOD conflict with source, '[$mylod name]' vs. '[$slod name]'"
	    return
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    ## Basic, all symbols _________________

    variable mytype   {} ; # Symbol type, 'tag', or 'branch'.
    variable mynr     {} ; # Revision number of a 'tag', branch number
			   # of a 'branch'.
    variable mysymbol {} ; # Reference to the symbol object of this
			   # symbol at the project level.
    variable mylod    {} ; # Reference to the line-of-development
			   # object the symbol belongs to. An
			   # alternative idiom would be to call it the
			   # branch the symbol is on. This reference
			   # is to a project-level object (symbol or
			   # trunk).

    ## Branch symbols _____________________

    variable mybranchparentrevnr {} ; # The number of the parent
				      # revision, derived from our
				      # branch number (mynr).
    variable mybranchparent      {} ; # Reference to the revision
				      # (object) which spawns the
				      # branch.
    variable mybranchchildrevnr  {} ; # Number of the first revision
				      # committed on this branch.
    variable mybranchchild       {} ; # Reference to the revision
				      # (object) first committed on
				      # this branch.
    variable mybranchposition    {} ; # Relative id of the branch in
				      # the file, to sort into
				      # creation order.

    ## Tag symbols ________________________

    variable mytagrev {} ; # Reference to the revision object the tag
			   # is on, identified by 'mynr'.

    # ... nothing special ... (only mynr, see basic)

    # # ## ### ##### ######## #############
    ## Internal methods

    proc SetupBranch {} {
	upvar 1 mybranchparentrevnr mybranchparentrevnr mynr mynr
	set mybranchparentrevnr [rev 2branchparentrevnr  $mynr]
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::file {
    namespace export sym
    namespace eval sym {
	namespace import ::vc::fossil::import::cvs::file::rev
	namespace import ::vc::tools::trouble
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file::sym 1.0
return
