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
package require vc::tools::log                      ; # User feedback
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
	set mytrunk      [$myproject trunk]
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
	set myheadrevnr $revnr
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

	if {[info exists myrev($revnr)]} {
	    trouble fatal "File $mypath contains duplicate definitions for revision $revnr."
	    return
	}

	set myaid($revnr) [$myproject defauthor $author]
	set myrev($revnr) [rev %AUTO% $revnr $date $state $self]

	$self RecordBasicDependencies $revnr $next
	return
    }

    method defdone {} {
	# This is all done after the revision tree has been extracted
	# from the file, before the commit mesages and delta texts are
	# processed.

	$self ProcessPrimaryDependencies
	$self ProcessBranchDependencies
	$self SortBranches
	$self ProcessTagDependencies
	$self DetermineTheRootRevision
	return
    }

    method setdesc {d} {# ignore}

    method extend {revnr commitmsg textrange} {
	set cmid [$myproject defcmessage [string trim $commitmsg]]

	set rev $myrev($revnr)

	if {[$rev hasmeta]} {
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

	# Determine the line of development for the revision (project
	# level). This gives us the branchid too, required for the
	# meta data group the revision is in. (Note: By putting both
	# branch/lod and project information into the group we ensure
	# that any cross-project and cross-branch commits are
	# separated into multiple commits, one in each of the projects
	# and/or branches).

	set lod [$self GetLOD $revnr]

	$rev setmeta [$myproject defmeta [$lod id] $myaid($revnr) $cmid]
	$rev settext $textrange
	$rev setlod  $lod

	# If this is revision 1.1, we have to determine whether the
	# file seems to have been created through 'cvs add' instead of
	# 'cvs import'. This can be done by looking at the un-
	# adulterated commit message, as CVS uses a hardwired magic
	# message for the latter, i.e. "Initial revision\n", no
	# period.  (This fact also helps us when the time comes to
	# determine whether this file might have had a default branch
	# in the past.)

	if {$revnr eq "1.1"} {
	    set myimported [expr {$commitmsg eq "Initial revision\n"}]
	}

	# Here we also keep track of the order in which the revisions
	# were added to the file.

	lappend myrevisions $rev
	return
    }

    method done {} {
	# Complete the revisions, branches, and tags. This includes
	# looking for a non-trunk default branch, marking its members
	# and linking them into the trunk.

	$self DetermineRevisionOperations
	$self DetermineLinesOfDevelopment
	$self HandleNonTrunkDefaultBranch
	$self RemoveIrrelevantDeletions
	$self RemoveInitialBranchDeletions

	if {[$myproject trunkonly]} {
	    $self ExcludeNonTrunkInformation
	}
	return
    }

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
    variable myaid      -array {} ; # Map revision numbers to the id
				    # of the author who committed
				    # it. This is later aggregated
				    # with commit message, branch name
				    # and project id for a meta id.
    variable myheadrevnr       {} ; # Head revision (revision number)
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

    variable mytrunk {} ; # Direct reference to myproject -> trunk.
    variable myroots {} ; # List of roots in the forest of
			  # lod's. Object references to revisions and
			  # branches. The latter can appear when they
			  # are severed from their parent.

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

    method RecordBasicDependencies {revnr next} {
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
	#                          parent -> child
	if {[rev istrunkrevnr $revnr]} {
	    lappend mydependencies $next $revnr
	} else {
	    lappend mydependencies $revnr $next
	}
	return
    }

    method ProcessPrimaryDependencies {} {
	foreach {parentrevnr childrevnr} $mydependencies {
	    set parent $myrev($parentrevnr)
	    set child  $myrev($childrevnr)
	    $parent setchild $child
	    $child setparent $parent
	}
	return
    }

    method ProcessBranchDependencies {} {
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
		$branch setparent $rev

		# If revisions were committed on the branch we store a
		# reference to the branch there, and further declare
		# the first child's parent to be branch's parent, and
		# list this child in the parent revision.

		if {[$branch haschild]} {
		    set childrevnr [$branch childrevnr]
		    set child $myrev($childrevnr)
		    $branch setchild $child

		    $child setparentbranch $branch
		    $child setparent       $rev
		    $rev addchildonbranch $child
		}
	    }
	}
	return
    }

    method SortBranches {} {
	foreach {revnr rev} [array get myrev] { $rev sortbranches }
	return
    }

    method ProcessTagDependencies {} {
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
		foreach tag $taglist {
		    $rev addtag $tag
		    $tag settagrev $rev
		}
	    }
	}
	return
    }

    method DetermineTheRootRevision {} {
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

	# In the future we also need a list, as branches can become
	# severed from their parent, making them their own root.
	set myroots [list $myroot]
	return
    }

    method DetermineRevisionOperations {} {
	foreach rev $myrevisions { $rev determineoperation }
	return
    }

    method DetermineLinesOfDevelopment {} {
	# For revisions this has been done already, in 'extend'. Now
	# we do this for the branches and tags.

	foreach {_ branch} [array get mybranches] {
	    $branch setlod [$self GetLOD [$branch parentrevnr]]
	}

	foreach {_ taglist} [array get mytags] {
	    foreach tag $taglist {
		$tag setlod [$self GetLOD [$tag tagrevnr]]
	    }
	}
	return
    }

    method GetLOD {revnr} {
	if {[rev istrunkrevnr $revnr]} {
	    return $mytrunk
	} else {
	    return [$self Rev2Branch $revnr]
	}
    }

    method HandleNonTrunkDefaultBranch {} {
	set revlist [$self NonTrunkDefaultRevisions]
	if {![llength $revlist]} return

	$self AdjustNonTrunkDefaultBranch $revlist
	$self CheckLODs
	return
    }

    method NonTrunkDefaultRevisions {} {
	# From cvs2svn the following explanation (with modifications
	# for our algorithm):

	# Determine whether there are any non-trunk default branch
	# revisions.

	# If a non-trunk default branch is determined to have existed,
	# return a list of objects for all revisions that were once
	# non-trunk default revisions, in dependency order (i.e. root
	# first).

	# There are two cases to handle:

	# One case is simple.  The RCS file lists a default branch
	# explicitly in its header, such as '1.1.1'.  In this case, we
	# know that every revision on the vendor branch is to be
	# treated as head of trunk at that point in time.

	# But there's also a degenerate case.  The RCS file does not
	# currently have a default branch, yet we can deduce that for
	# some period in the past it probably *did* have one.  For
	# example, the file has vendor revisions 1.1.1.1 -> 1.1.1.96,
	# all of which are dated before 1.2, and then it has 1.1.1.97
	# -> 1.1.1.100 dated after 1.2.  In this case, we should
	# record 1.1.1.96 as the last vendor revision to have been the
	# head of the default branch.

	if {$myprincipal ne ""} {
	    # There is still a default branch; that means that all
	    # revisions on that branch get marked.

	    log write 5 file "Found explicitly marked NTDB"

	    set rnext [$myroot child]
	    if {$rnext ne ""} {
		trouble fatal "File with default branch $myprincipal also has revision [$rnext revnr]"
		return
	    }

	    set rev [$mybranches($myprincipal) child]
	    set res {}

	    while {$rev ne ""} {
		lappend res $rev
		set rev [$rev child]
	    }

	    return $res

	} elseif {$myimported} {
	    # No default branch, but the file appears to have been
	    # imported.  So our educated guess is that all revisions
	    # on the '1.1.1' branch with timestamps prior to the
	    # timestamp of '1.2' were non-trunk default branch
	    # revisions.
	    
	    # This really only processes standard '1.1.1.*'-style
	    # vendor revisions.  One could conceivably have a file
	    # whose default branch is 1.1.3 or whatever, or was that
	    # at some point in time, with vendor revisions 1.1.3.1,
	    # 1.1.3.2, etc.  But with the default branch gone now,
	    # we'd have no basis for assuming that the non-standard
	    # vendor branch had ever been the default branch anyway.
	    
	    # Note that we rely on comparisons between the timestamps
	    # of the revisions on the vendor branch and that of
	    # revision 1.2, even though the timestamps might be
	    # incorrect due to clock skew.  We could do a slightly
	    # better job if we used the changeset timestamps, as it is
	    # possible that the dependencies that went into
	    # determining those timestamps are more accurate.  But
	    # that would require an extra pass or two.

	    if {![info exists mybranches(1.1.1)]} { return {} }

	    log write 5 file "Deduced existence of NTDB"

	    set rev  [$mybranches(1.1.1) child]
	    set res  {}
	    set stop [$myroot child]

	    if {$stop eq ""} {
		# Get everything on the branch
		while {$rev ne ""} {
		    lappend res $rev
		    set rev [$rev child]
		}
	    } else {
		# Collect everything on the branch which seems to have
		# been committed before the first primary child of the
		# root revision.
		set stopdate [$stop date]
		while {$rev ne ""} {
		    if {[$rev date] >= $stopdate} break
		    lappend res $rev
		    set rev [$rev child]
		}
	    }

	    return $res

	} else {
	    return {}
	}
    }

    method AdjustNonTrunkDefaultBranch {revlist} {
	set stop [$myroot child] ;# rev '1.2'

	log write 5 file "Adjusting NTDB containing [nsp [llength $revlist] revision]"

	# From cvs2svn the following explanation (with modifications
	# for our algorithm):

	# Adjust the non-trunk default branch revisions found in the
	# 'revlist'.

	# 'myimported' is a boolean flag indicating whether this file
	# appears to have been imported, which also means that
	# revision 1.1 has a generated log message that need not be
	# preserved.  'revlist' is a list of object references for the
	# revisions that have been determined to be non-trunk default
	# branch revisions.

	# Note that the first revision on the default branch is
	# handled strangely by CVS.  If a file is imported (as opposed
	# to being added), CVS creates a 1.1 revision, then creates a
	# vendor branch 1.1.1 based on 1.1, then creates a 1.1.1.1
	# revision that is identical to the 1.1 revision (i.e., its
	# deltatext is empty).  The log message that the user typed
	# when importing is stored with the 1.1.1.1 revision.  The 1.1
	# revision always contains a standard, generated log message,
	# 'Initial revision\n'.

	# When we detect a straightforward import like this, we want
	# to handle it by deleting the 1.1 revision (which doesn't
	# contain any useful information) and making 1.1.1.1 into an
	# independent root in the file's dependency tree.  In SVN,
	# 1.1.1.1 will be added directly to the vendor branch with its
	# initial content.  Then in a special 'post-commit', the
	# 1.1.1.1 revision is copied back to trunk.

	# If the user imports again to the same vendor branch, then CVS
	# creates revisions 1.1.1.2, 1.1.1.3, etc. on the vendor branch,
	# *without* counterparts in trunk (even though these revisions
	# effectively play the role of trunk revisions).  So after we add
	# such revisions to the vendor branch, we also copy them back to
	# trunk in post-commits.

	# We mark the revisions found in 'revlist' as default branch
	# revisions.  Also, if the root revision has a primary child
	# we set that revision to depend on the last non-trunk default
	# branch revision and possibly adjust its type accordingly.

	set first [lindex $revlist 0]

	log write 6 file "<[$first revnr]> [expr {$myimported ? "imported" : "not imported"}], [$first operation], [expr {[$first hastext] ? "has text" : "no text"}]"

	if {$myimported &&
	    [$first revnr] eq "1.1.1.1" &&
	    [$first operation] eq "change" &&
	    ![$first hastext]} {

	    set rev11 [$first parent] ; # Assert: Should be myroot.
	    log write 3 file "Removing irrelevant revision [$rev11 revnr]"

	    # Cut out the old myroot revision.

	    ldelete myroots $rev11 ; # Not a root any longer.
	    unset myrev([$rev11 revnr])

	    $first cutfromparent ; # Sever revision from parent revision.
	    if {$stop ne ""} {
		$stop cutfromparent
		lappend myroots $stop ; # New root, after vendor branch
	    }

	    # Cut out the vendor branch symbol

	    set vendor [$first parentbranch]
	    if {$vendor eq ""} { trouble internal "First NTDB revision has no branch" }
	    if {[$vendor parent] eq $rev11} {
		unset mybranches([$vendor branchnr])
		$rev11 removebranch        $vendor
		$rev11 removechildonbranch $first
		$first cutfromparentbranch
		lappend myroots $first
	    }

	    # Change the type of first (typically from Change to Add):
	    $first retype add

	    # Move any tags and branches from the old to the new root.
	    $rev11 movesymbolsto $first
	}

	# Mark all the special revisions as such
	foreach rev $revlist {
	    log write 3 file "Revision on default branch: [$rev revnr]"
	    $rev isondefaultbranch
	}

	if {$stop ne ""} {
	    # Revision 1.2 logically follows the imported revisions,
	    # not 1.1.  Accordingly, connect it to the last NTDBR and
	    # possibly change its type.

	    set last [lindex $revlist end]
	    $stop setdefaultbranchparent $last ; # Retypes the revision too.
	    $last setdefaultbranchchild  $stop
	}
	return
    }

    method CheckLODs {} {
	foreach {_ branch}  [array get mybranches] { $branch checklod }
	foreach {_ taglist} [array get mytags] {
	    foreach tag $taglist { $tag checklod }
	}
	return
    }

    method RemoveIrrelevantDeletions {} {
    }

    method RemoveInitialBranchDeletions {} {
    }

    method ExcludeNonTrunkInformation {} {
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
	namespace import ::vc::tools::log
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file 1.0
return
