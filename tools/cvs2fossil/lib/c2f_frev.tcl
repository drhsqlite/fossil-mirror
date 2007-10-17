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

## Revisions per file.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require vc::tools::misc                     ; # Text formatting

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {revnr date state thefile} {
	set myrevnr    $revnr
	set mydate     $date
	set myorigdate $date
	set mystate    $state
	set myfile     $thefile
	return
    }

    # Basic pieces ________________________

    method hasmeta {} { return [expr {$mymetaid ne ""}] }
    method hastext {} {
	struct::list assign $mytext s e
	return [expr {$s <= $e}]
    }

    method setmeta {meta} { set mymetaid $meta ; return }
    method settext {text} { set mytext   $text ; return }
    method setlod  {lod}  { set mylod    $lod  ; return }

    method revnr {} { return $myrevnr }
    method state {} { return $mystate }
    method lod   {} { return $mylod   }
    method date  {} { return $mydate  }

    # Basic parent/child linkage __________

    method hasparent {} { return [expr {$myparent ne ""}] }
    method haschild  {} { return [expr {$mychild  ne ""}] }

    method setparent {parent} {
	if {$myparent ne ""} { trouble internal "Parent already defined" }
	set myparent $parent
	return
    }

    method cutfromparent {} { set myparent "" ; return }
    method cutfromchild  {} { set mychild  "" ; return }

    method setchild {child} {
	if {$mychild ne ""} { trouble internal "Child already defined" }
	set mychild $child
	return
    }

    method parent {} { return $myparent }
    method child  {} { return $mychild  }

    # Branch linkage ______________________

    method setparentbranch {branch} {
	if {$myparentbranch ne ""} { trouble internal "Branch parent already defined" }
	set myparentbranch $branch
	return
    }

    method parentbranch {} { return $myparentbranch }

    method addbranch {branch} {
	lappend mybranches $branch
	return
    }

    method addchildonbranch {child} {
	lappend mybranchchildren $child
	return
    }

    method cutfromparentbranch {} { set myparentbranch "" ; return }

    method removebranch {branch} {
	ldelete mybranches $branch
	return
    }

    method removechildonbranch {rev} {
	ldelete mybranchchildren $rev
	return
    }

    method sortbranches {} {
	if {![llength $mybranches]} return

	# Sort the branches spawned by this revision in creation
	# order. To help in this our file gave all branches a position
	# id, in order of their definition by the RCS archive.
	#
	# The creation order is (apparently) the reverse of the
	# definition order. (If a branch is created then deleted, a
	# later branch can be assigned the recycled branch number;
	# therefore branch numbers are not an indication of creation
	# order.)

	set tmp {}
	foreach branch $mybranches {
	    lappend tmp [list $branch [$branch position]]
	}

	set mybranches {}
	foreach item [lsort -index 1 -decreasing $tmp] {
	    struct::list assign $item branch position
	    lappend mybranches $branch
	}
	return
    }

    method movebranchesto {rev} {
	set revlod [$rev lod]
	foreach branch $mybranches {
	    $rev addbranch $branch
	    $branch setparent $rev
	    $branch setlod $revlod
	}
	foreach branchrev $mybranchchildren {
	    $rev addchildonbranch $branchrev
	    $branchrev cutfromparent
	    $branchrev setparent $rev
	}
	set mybranches       {}
	set mybranchchildren {}
	return
    }

    # Tag linkage _________________________

    method addtag {tag} {
	lappend mytags $tag
	return
    }

    method movetagsto {rev} {
	set revlod [$rev lod]
	foreach tag $mytags {
	    $rev addtag $tag
	    $tag settagrev $rev
	    $tag setlod $revlod
	}
	set mytags {}
	return
    }

    # general symbol operations ___________

    method movesymbolsto {rev} {
	# Move the tags and branches attached to this revision to the
	# destination and fix all pointers.

	$self movetagsto     $rev
	$self movebranchesto $rev
	return
    }

    # Derived stuff _______________________

    method determineoperation {} {
	# Look at the state of both this revision and its parent to
	# determine the type opf operation which was performed (add,
	# modify, delete, none).
	#
	# The important information is dead vs not-dead for both,
	# giving rise to four possible types.

	set sdead [expr {$mystate eq "dead"}]
	set pdead [expr {$myparent eq "" || [$myparent state] eq "dead"}]

	set myoperation $myopstate([list $pdead $sdead])
	return
    }

    method operation {} { return $myoperation }
    method retype {x} { set myoperation $x ; return }

    method isondefaultbranch {} { set myisondefaultbranch 1 ; return }

    method setdefaultbranchchild  {rev} { set mydbchild $rev ; return }
    method setdefaultbranchparent {rev} {
	set mydbparent $rev

	# Retype the revision (may change from 'add' to 'change').

	set sdead [expr {$myoperation     ne "change"}]
	set pdead [expr {[$rev operation] ne "change"}]
	set myoperation $myopstate([list $pdead $sdead])
	return
    }

    # # ## ### ##### ######## #############
    ## Type API

    typemethod istrunkrevnr {revnr} {
	return [expr {[llength [split $revnr .]] == 2}]
    }

    typemethod isbranchrevnr {revnr _ bv} {
	if {[regexp $mybranchpattern $revnr -> head tail]} {
	    upvar 1 $bv branchnr
	    set branchnr ${head}$tail
	    return 1
	}
	return 0
    }

    typemethod 2branchnr {revnr} {
	# Input is a branch revision number, i.e. a revision number
	# with an even number of components; for example '2.9.2.1'
	# (never '2.9.2' nor '2.9.0.2').  The return value is the
	# branch number (for example, '2.9.2').  For trunk revisions,
	# like '3.4', we return the empty string.

	if {[$type istrunkrevnr $revnr]} {
	    return ""
	}
	return [join [lrange [split $revnr .] 0 end-1] .]
    }

    typemethod 2branchparentrevnr {branchnr} {
	# Chop the last segment off
	return [join [lrange [split $branchnr .] 0 end-1] .]
    }

    # # ## ### ##### ######## #############
    ## State

    typevariable mybranchpattern {^((?:\d+\.\d+\.)+)(?:0\.)?(\d+)$}
    # First a nonzero even number of digit groups with trailing dot
    # CVS then sticks an extra 0 in here; RCS does not.
    # And the last digit group.

    variable myrevnr     {} ; # Revision number of the revision.
    variable mydate      {} ; # Timestamp of the revision, seconds since epoch
    variable myorigdate  {} ; # Original unmodified timestamp.
    variable mystate     {} ; # State of the revision.
    variable myfile      {} ; # Ref to the file object the revision belongs to.
    variable mytext      {} ; # Range of the (delta) text for this revision in the file.
    variable mymetaid    {} ; # Id of the meta data group the revision
			      # belongs to. This is later used to put
			      # the file revisions into preliminary
			      # changesets (aka project revisions).
			      # This id encodes 4 pieces of data,
			      # namely: the project and branch the
			      # revision was committed to, the author
			      # who did the commit, and the message
			      # used.
    variable mylod       {} ; # Reference to the line-of-development
			      # object the revision belongs to. An
			      # alternative idiom would be to call it
			      # the branch the revision is on. This
			      # reference is to a project-level object
			      # (symbol or trunk).

    # Basic parent/child linkage (lines of development)

    variable myparent {} ; # Ref to parent revision object. Link required because of
    #                    ; # 'cvsadmin -o', which can create arbitrary gaps in the
    #                    ; # numbering sequence. This is in the same line of development
    #                    ; # Note: For the first revision on a branch the revision
    #                    ; # it was spawned from is the parent. Only the root revision
    #                    ; # of myfile's revision tree has nothing set here.
    #                    ; #

    variable mychild  {} ; # Ref to the primary child revision object, i.e. the next
    #                    ; # revision in the same line of development.

    # Branch linkage ____________________

    variable mybranches     {} ; # List of the branches (objs) spawned by this revision.
    variable myparentbranch {} ; # For the first revision on a branch the relevant
    #                          ; # branch object. This also allows us to determine if
    #                          ; # myparent is in the same LOD, or the revision the
    #                          ; # branch spawned from.

    # List of the revision objects of the first commits on any
    # branches spawned by this revision on which commits occurred.
    # This dependency is kept explicitly because otherwise a
    # revision-only topological sort would miss the dependency that
    # exists via -> mybranches.

    variable mybranchchildren {} ; # List of the revisions (objs) which are the first
    #                            ; # commits on any of the branches spawned from this
    #                            ; # revision. The dependency is kept explicitly to
    #                            ; # ensure that a revision-only topological sort will
    #                            ; # not miss it, as it otherwise exists only via
    #                            ; # mybranches.

    # Tag linkage ________________________

    variable mytags {} ; # List of tags (objs) associated with this revision.

    # More derived data

    variable myoperation        {} ; # One of 'add', 'change', 'delete', or
			             # 'nothing'. Derived from our and
			             # its parent's state.
    variable myisondefaultbranch 0 ; # Boolean flag, set if the
				     # revision is on the non-trunk
				     # default branch, aka vendor
				     # branch.
    variable mydbparent         {} ; # Reference to the last revision
				     # on the vendor branch if this is
				     # the primary child of the
				     # regular root.
    variable mydbchild          {} ; # Reference to the primary child
				     # of the regular root if this is
				     # the last revision on the vendor
				     # branch.

    # dead(self) x dead(parent) -> operation
    typevariable myopstate -array {
	{0 0} change
	{0 1} delete
	{1 0} add
	{1 1} nothing
    }

    # # ## ### ##### ######## #############
    ## Internal methods

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs::file {
    namespace export rev
    namespace eval rev {
	namespace import ::vc::tools::misc::*
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file::rev 1.0
return
