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

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {revnr date author state thefile} {
	set myrevnr    $revnr
	set mydate     $date
	set myorigdate $date
	set myauthor   $author
	set mystate    $state
	set myfile     $thefile
	return
    }

    # Basic pieces ________________________

    method hascommitmsg {} { return $myhascm }

    method setcommitmsg  {cm}   { set mycommitmsg  $cm   ; set myhascm 1 ; return }
    method settext       {text} { set mytext       $text ; return }
    method setbranchname {name} { set mybranchname $name ; return }

    method revnr {} { return $myrevnr }

    # Basic parent/child linkage __________

    method hasparent {} { return [expr {$myparent ne ""}] }
    method haschild  {} { return [expr {$mychild  ne ""}] }

    method setparent {parent} {
	if {$myparent ne ""} { trouble internal "Parent already defined" }
	set myparent $parent
	return
    }

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

    method addbranch {branch} {
	lappend mybranches $branch
	#sorted in ascending order by branch number?
	return
    }

    method addchildonbranch {child} {
	lappend mybranchchildren $child
	return
    }

    # Tag linkage _________________________

    method addtag {tag} {
	lappend mytags $tag
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
	    struct::list assign $item -> branch position
	    lappend mybranches $branch
	}
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
    variable myhascm     0  ; # Bool flag, set when the commit msg was set.
    variable mytext      {} ; # Range of the (delta) text for this revision in the file.

    # The meta data block used later to group revisions into changesets.
    # The project name factors into this as well, but is not stored
    # here. The name is acessible via myfile's project.

    variable myauthor     {} ; # Name of the user who committed the revision.
    variable mycommitmsg  {} ; # The message entered as part of the commit.
    variable mybranchname {} ; # The name of the branch the revision was committed on.

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
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file::rev 1.0
return
