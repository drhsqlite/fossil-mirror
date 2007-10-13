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

## File, part of a project, part of a CVS repository. Multiple
## instances are possible.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require struct::set                         ; # Set operations.
package require vc::fossil::import::cvs::file::rev  ; # CVS per file revisions.
package require vc::fossil::import::cvs::file::sym  ; # CVS per file symbols.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::misc                     ; # Text formatting

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::file {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {path executable project} {
	set mypath       $path
	set myexecutable $executable
	set myproject    $project
	return
    }

    method path    {} { return $mypath }
    method project {} { return $myproject }

    # # ## ### ##### ######## #############
    ## Methods required for the class to be a sink of the rcs parser

    #method begin {} {puts begin}
    #method sethead {h} {puts head=$h}
    #method setprincipalbranch {b} {puts pb=$b}
    #method deftag {s r} {puts $s=$r}
    #method setcomment {c} {puts comment=$c}
    #method admindone {} {puts admindone}
    #method def {rev date author state next branches} {puts "def $rev $date $author $state $next $branches"}
    #method defdone {} {puts def-done}
    #method setdesc {d} {puts desc=$d}
    #method extend {rev commitmsg deltarange} {puts "extend $commitmsg $deltarange"}
    #method done {} {puts done}

    # # ## ### ##### ######## #############
    ## Persistence (pass II)

    method persist {} {
    }

    # # ## ### ##### ######## #############
    ## Implement the sink

    method begin {} {#ignore}

    method sethead {revnr} {
	set myhead $revnr
	return
    }

    method setprincipalbranch {branchnr} {
	set myprincipal $branchnr
	return
    }

    method deftag {name revnr} {
	# FUTURE: Perform symbol transformation here.

	if {[struct::set contains $mysymbols $name]} {
	    trouble fatal "Multiple definitions of the symbol '$name' in '$mypath'"
	    return
	}

	struct::set add mysymbols $name

	if {[rev isbranchrevnr $revnr -> branchnr]} {
	    $self AddBranch $name $branchnr
	} else {
	    $self AddTag $name $revnr
	}
	return
    }

    method setcomment {c} {# ignore}

    method admindone {} {
	# We do nothing at the boundary of admin and revision data
    }

    method def {revnr date author state next branches} {
	$self RecordBranchCommits $branches
	$myproject author $author

	if {[info exists myrev($revnr)]} {
	    trouble fatal "File $mypath contains duplicate definitions for revision $revnr."
	    return
	}

	set myrev($revnr) [rev %AUTO% $revnr $date $author $state $self]

	RecordBasicDependencies $revnr $next
	return
    }

    method defdone {} {
	# This is all done after the revision tree has been extracted
	# from the file, before the commit mesages and delta texts are
	# processed.

	ProcessPrimaryDependencies
	ProcessBranchDependencies
	SortBranches
	ProcessTagDependencies
	DetermineTheRootRevision
	return
    }

    method setdesc {d} {# ignore}

    method extend {revnr commitmsg deltarange} {
	set cm [string trim $commitmsg]
	$myproject cmessage $cm

	set rev $myrev($revnr)

	if {[$rev hascommitmsg]} {
	    # Apparently repositories exist in which the delta data
	    # for revision 1.1 is provided several times, at least
	    # twice. The actual cause of this duplication is not
	    # known. Speculation centers on RCS/CVS bugs, or from
	    # manual edits of the repository which borked the
	    # internals. Whatever the cause, testing showed that both
	    # cvs and rcs use the first definition when performing a
	    # checkout, and we follow their lead. Side notes: 'cvs
	    # log' fails on such a file, and 'cvs rlog' prints the log
	    # message from the first delta, ignoring the second.

	    log write 1 file "In file $mypath : Duplicate delta data for revision $revnr"
	    log write 1 file "Ignoring the duplicate"
	    return
	}

	# Extend the revision with the new information. The revision
	# object uses this to complete its meta data set.

	$rev setcommitmsg $cm
	$rev settext  $deltarange

	if {![rev istrunkrevnr $revnr]} {
	    $rev setbranchname [[$self Rev2Branch $revnr] name]
	}

	# If this is revision 1.1, we have to determine whether the
	# file seems to have been created through 'cvs add' instead of
	# 'cvs import'. This can be done by looking at the un-
	# adulterated commit message, as CVS uses a hardwired magic
	# message for the latter, i.e. "Initial revision\n", no
	# period.  (This fact also helps us when the time comes to
	# determine whether this file might have had a default branch
	# in the past.)

	if {$revnr eq ""} {
	    set myimported [expr {$commitmsg eq "Initial revision\n"}]
	}

	# Here we also keep track of the order in which the revisions
	# were added to the file.

	lappend myrevisions $rev
	return
    }

    method done {} {}

    # # ## ### ##### ######## #############
    ## State

    variable mypath            {} ; # Path of the file's rcs archive.
    variable myexecutable      0  ; # Boolean flag 'file executable'.
    variable myproject         {} ; # Reference to the project object
				    # the file belongs to.
    variable myrev -array      {} ; # Maps revision number to the
				    # associated revision object.
    variable myrevisions       {} ; # Same as myrev, but a list,
				    # giving us the order of
				    # revisions.
    variable myhead            {} ; # Head revision (revision number)
    variable myprincipal       {} ; # Principal branch (branch number).
				    # Contrary to the name this is the
				    # default branch.
    variable mydependencies    {} ; # Dictionary parent -> child,
				    # records primary dependencies.
    variable myimported        0  ; # Boolean flag. Set if and only if
				    # rev 1.1 of the file seemingly
				    # was imported instead of added
				    # normally.
    variable myroot            {} ; # Reference to the revision object
				    # holding the root revision.  Its
				    # number usually is '1.1'. Can be
				    # a different number, because of
				    # gaps created via 'cvsadmin -o'.
    variable mybranches -array {} ; # Maps branch number to the symbol
				    # object handling the branch.
    variable mytags     -array {} ; # Maps revision number to the list
				    # of symbol objects for the tags
				    # associated with the revision.
    variable mysymbols         {} ; # Set of the symbol names found in
				    # this file.

    variable mybranchcnt 0 ; # Counter for branches, to record their
			     # order of definition. This also defines
			     # their order of creation, which is the
			     # reverse of definition.  I.e. a smaller
			     # number means 'Defined earlier', means
			     # 'Created later'.

    ### TODO ###
    ### RCS mode info (kb, kkb, ...)

    # # ## ### ##### ######## #############
    ## Internal methods

    method RecordBranchCommits {branches} {
	foreach branchrevnr $branches {
	    if {[catch {
		set branch [$self Rev2Branch $branchrevnr]
	    }]} {
		set branch [$self AddUnlabeledBranch [rev 2branchnr $branchrevnr]]
	    }

	    # Record the commit, just as revision number for
	    # now. ProcesBranchDependencies will extend that ito a
	    # proper object reference.

	    $branch setchildrevnr $branchrevnr
	}
	return
    }

    method Rev2Branch {revnr} {
	if {[rev istrunkrevnr $revnr]} {
	    trouble internal "Expected a branch revision number"
	}
	return $mybranches([rev 2branchnr $revnr])
    }

    method AddUnlabeledBranch {branchnr} {
	return [$self AddBranch unlabeled-$branchnr $branchnr]
    }

    method AddBranch {name branchnr} {
	if {[info exists mybranches($branchnr)]} {
	    log write 1 file "In '$mypath': Branch '$branchnr' named '[$mybranches($branchnr) name]'"
	    log write 1 file "Cannot have second name '$name', ignoring it"
	    return
	}
	set branch [sym %AUTO% branch $branchnr [$myproject getsymbol $name]]
	$branch setposition [incr mybranchcnt]
	set mybranches($branchnr) $branch
	return $branch
    }

    method AddTag {name revnr} {
	set tag [sym %AUTO% tag $revnr [$myproject getsymbol $name]]
	lappend mytags($revnr) $tag
	return $tag
    }

    proc RecordBasicDependencies {revnr next} {
	# Handle the revision dependencies. Record them for now, do
	# nothing with them yet.

	# On the trunk the 'next' field points to the previous
	# revision, i.e. the _parent_ of the current one. Example:
	# 1.6's next is 1.5 (modulo cvs admin -o).

	# Contrarily on a branch the 'next' field points to the
	# primary _child_ of the current revision. As example,
	# 1.1.3.2's 'next' will be 1.1.3.3.

	# The 'next' field actually always refers to the revision
	# containing the delta needed to retrieve that revision.

	# The dependencies needed here are the logical structure,
	# parent/child, and not the implementation dependent delta
	# pointers.

	if {$next eq ""} return

	upvar 1 mydependencies mydependencies

	#                          parent -> child
	if {[rev istrunkrevnr $revnr]} {
	    lappend mydependencies $next $revnr
	} else {
	    lappend mydependencies $revnr $next
	}
	return
    }

    proc ProcessPrimaryDependencies {} {
	upvar 1 mydependencies mydependencies myrev myrev

	foreach {parentrevnr childrevnr} $mydependencies {
	    set parent $myrev($parentrevnr)
	    set child  $myrev($childrevnr)
	    $parent setchild $child
	    $child setparent $parent
	}
	return
    }

    proc ProcessBranchDependencies {} {
	upvar 1 mybranches mybranches myrev myrev

	foreach {branchnr branch} [array get mybranches] {
	    set revnr [$branch parentrevnr]

	    if {![info exists myrev($revnr)]} {
		log write 1 file "In '$mypath': The branch '[$branch name]' references"
		log write 1 file "the bogus revision '$revnr' and will be ignored."
		$branch destroy
		unset mybranches($branchnr)
	    } else {
		set rev $myrev($revnr)
		$rev addbranch $branch

		# If revisions were committed on the branch we store a
		# reference to the branch there, and further declare
		# the first child's parent to be branch's parent, and
		# list this child in the parent revision.

		if {[$branch haschild]} {
		    set childrevnr [$branch childrevnr]
		    set child $myrev($childrevnr)

		    $child setparentbranch $branch
		    $child setparent       $rev
		    $rev addchildonbranch $child
		}
	    }
	}
	return
    }

    proc SortBranches {} {
	upvar 1 myrev myrev

	foreach {revnr rev} [array get myrev] {
	    $rev sortbranches
	}
	return
    }

    proc ProcessTagDependencies {} {
	upvar 1 mytags mytags myrev myrev

	foreach {revnr taglist} [array get mytags] {
	    if {![info exists myrev($revnr)]} {
		set n [llength $taglist]
		log write 1 file "In '$mypath': The following [nsp $n tag] reference"
		log write 1 file "the bogus revision '$revnr' and will be ignored."
		foreach tag $taglist {
		    log write 1 file "    [$tag name]"
		    $tag destroy
		}
		unset mytags($revnr)
	    } else {
		set rev $myrev($revnr)
		foreach tag $taglist { $rev addtag $tag }
	    }
	}
	return
    }

    proc DetermineTheRootRevision {} {
	upvar 1 myrev myrev myroot myroot

	# The root is the one revision which has no parent. By
	# checking all revisions we ensure that we can detect and
	# report the case of multiple roots. Without that we could
	# simply take one revision and follow the parent links to
	# their root (sic!).

	foreach {revnr rev} [array get myrev] {
	    if {[$rev hasparent]} continue
	    if {$myroot ne ""} { trouble internal "Multiple root revisions found" }
	    set myroot $rev
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.
    pragma -simpledispatch yes ; # simple fast dispatch

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export file
    namespace eval file {
	# Import not required, already a child namespace.
	# namespace import ::vc::fossil::import::cvs::file::rev
	# namespace import ::vc::fossil::import::cvs::file::sym
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file 1.0
return
