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

## Symbols (Tags, Branches) per file.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::fossil::import::cvs::file::rev  ; # CVS per file revisions.
package require vc::fossil::import::cvs::state      ; # State storage.
package require vc::fossil::import::cvs::integrity  ; # State integrity checks.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::file::sym {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {symtype nr symbol file} {
	set myfile   $file
	set mytype   $symtype
	set mynr     $nr
	set mysymbol $symbol

	switch -exact -- $mytype {
	    branch  { SetupBranch ; return }
	    tag     { return }
	}
	integrity assert 0 {Bad symbol type '$mytype'}
	return
    }

    method defid {} {
	set myid [incr myidcounter]
	return
    }

    method fid    {} { return $myid     }
    method symbol {} { return $mysymbol }

    # Symbol acessor methods.

    delegate method name to mysymbol
    delegate method id   to mysymbol

    # Symbol aggregation methods

    delegate method countasbranch to mysymbol
    delegate method countastag    to mysymbol
    delegate method countacommit  to mysymbol

    method blockedby {fsymbol} {
	$mysymbol blockedby [$fsymbol symbol]
	return
    }

    method possibleparents {} {
	switch -exact -- $mytype {
	    branch { $self BranchParents }
	    tag    { $self TagParents    }
	}
	return
    }

    method BranchParents {} {
	# The "obvious" parent of a branch is the branch holding the
	# revision spawning the branch. Any other branches that are
	# rooted at the same revision and were committed earlier than
	# the branch are also possible parents.

	# Ignore this if the branch symbol is detached.
	if {$mybranchparent eq ""} return

	$mysymbol possibleparent [[$mybranchparent lod] symbol]

	foreach branch [$mybranchparent branches] {
	    # A branch cannot be its own parent. Nor can a branch
	    # created after this one be its parent. This means that we
	    # can abort the loop when we have reached ourselves in the
	    # list of branches. Here the order of file::rev.mybranches
	    # comes into play, as created by file::rev::sortbranches.

	    if {$branch eq $self} break
	    $mysymbol possibleparent [$branch symbol]
	}
	return
    }

    method TagParents {} {
	# The "obvious" parent of a tag is the branch holding the
	# revision spawning the tag. Branches that are spawned by the
	# same revision are also possible parents.

	$mysymbol possibleparent [[$mytagrev lod] symbol]

	foreach branch [$mytagrev branches] {
	    $mysymbol possibleparent [$branch symbol]
	}
	return
    }

    #

    method istrunk {} { return 0 }

    # Branch acessor methods.

    method setchildrevnr  {revnr} {
	integrity assert {$mybranchchildrevnr eq ""} {Child already defined}
	set mybranchchildrevnr $revnr
	return
    }

    method setposition  {n}   { set mybranchposition $n ; return }
    method setparent    {rev} { set mybranchparent $rev ; return }
    method setchild     {rev} { set mybranchchild  $rev ; return }
    method cutchild     {}    { set mybranchchild  ""   ; return }
    method cutbranchparent {} { set mybranchparent ""   ; return }

    method branchnr    {} { return $mynr }
    method parentrevnr {} { return $mybranchparentrevnr }
    method childrevnr  {} { return $mybranchchildrevnr }
    method haschildrev {} { return [expr {$mybranchchildrevnr ne ""}] }
    method haschild    {} { return [expr {$mybranchchild ne ""}] }
    method parent      {} { return $mybranchparent }
    method child       {} { return $mybranchchild }
    method position    {} { return $mybranchposition }

    # Tag acessor methods.

    method tagrevnr  {}    { return $mynr }
    method settagrev {rev} { set mytagrev $rev ; return }

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
	    branch  {
		# However, ignore this if the branch symbol is
		# detached.
		if {$mybranchparent eq ""} return

		set slod [$mybranchparent lod]
	    }
	    tag     { set slod [$mytagrev       lod] }
	}

	if {$mylod ne $slod} {
	    trouble fatal "For $mytype [$mysymbol name]: LOD conflict with source, '[$mylod name]' vs. '[$slod name]'"
	    return
	}
	return
    }

    # # ## ### ##### ######## #############

    method persist {} {
	# Save the information we need after the collection pass.

	set fid [$myfile   id]
	set sid [$mysymbol id]
	set lod [$mylod    id]

	switch -exact -- $mytype {
	    tag {
		set rid [$mytagrev id]
		state transaction {
		    state run {
			INSERT INTO tag ( tid,   fid,  lod,  sid,  rev)
			VALUES          ($myid, $fid, $lod, $sid, $rid);
		    }
		}
	    }
	    branch {
		lappend map @F@ [expr { ($mybranchchild  eq "") ? "NULL" : [$mybranchchild  id] }]
		lappend map @P@ [expr { ($mybranchparent eq "") ? "NULL" : [$mybranchparent id] }]

		set cmd {
		    INSERT INTO branch ( bid,   fid,  lod,  sid,  root, first, bra,  pos              )
		    VALUES             ($myid, $fid, $lod, $sid,  @P@,  @F@,  $mynr, $mybranchposition);
		}
		state transaction {
		    state run [string map $map $cmd]
		}
	    }
	}

	return
    }

    method DUMP {label} {
	puts "$label = $self $mytype [$self name] \{"
	switch -exact -- $mytype {
	    tag {
		puts "\tR\t$mytagrev"
	    }
	    branch {
		puts "\tP\t$mybranchparent"
		puts "\tC\t$mybranchchild"
		puts "\t\t<$mynr>"
	    }
	}
	puts "\}"
	return
    }

    # # ## ### ##### ######## #############
    ## State

    # Persistent:
    #        Tag: myid           - tag.tid
    #             myfile         - tag.fid
    #             mylod          - tag.lod
    #             mysymbol       - tag.sid
    #             mytagrev       - tag.rev
    #
    #     Branch: myid           - branch.bid
    #		  myfile         - branch.fid
    #		  mylod          - branch.lod
    #             mysymbol       - branch.sid
    #             mybranchparent - branch.root
    #             mybranchchild  - branch.first
    #             mynr           - branch.bra

    typevariable myidcounter 0 ; # Counter for symbol ids.
    variable myid           {} ; # Symbol id.

    ## Basic, all symbols _________________

    variable myfile   {} ; # Reference to the file the symbol is in.
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
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::tools::trouble
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file::sym 1.0
return
