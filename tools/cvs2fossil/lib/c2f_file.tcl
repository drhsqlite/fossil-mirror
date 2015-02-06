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

## File, part of a project, part of a CVS repository. Multiple
## instances are possible.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                             ; # Required runtime.
package require snit                                ; # OO system.
package require struct::set                         ; # Set operations.
package require struct::list                        ; # Higher order operations.
package require vc::fossil::import::cvs::blobstore  ; # Blob storage.
package require vc::fossil::import::cvs::file::rev  ; # CVS per file revisions.
package require vc::fossil::import::cvs::file::sym  ; # CVS per file symbols.
package require vc::fossil::import::cvs::state      ; # State storage.
package require vc::fossil::import::cvs::integrity  ; # State integrity checks.
package require vc::fossil::import::cvs::gtcore     ; # Graph traversal core.
package require vc::tools::trouble                  ; # Error reporting.
package require vc::tools::log                      ; # User feedback
package require vc::tools::misc                     ; # Text formatting

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::file {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {id path usrpath executable project} {
	set myid         $id
	set mypath       $path
	set myusrpath    $usrpath
	set myexecutable $executable
	set myproject    $project
	set mytrunk      [$myproject trunk]
	set myblob       [blobstore ${selfns}::%AUTO% $id]
	return
    }

    method setid {id} {
	integrity assert {$myid eq ""} {File '$mypath' already has an id, '$myid'}
	set myid $id
	$myblob setid $id
	return
    }

    method id      {} { return $myid }
    method path    {} { return $mypath }
    method usrpath {} { return $myusrpath }
    method project {} { return $myproject }

    delegate method commitmessageof to myproject

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
	# First collect the reachable revisions and symbols, then
	# assign id's to all. They are sorted so that we will have ids
	# which sort in order of creation. Then we can save them. This
	# is done bottom up. Revisions, then symbols. __NOTE__ This
	# works only because sqlite is not checking foreign key
	# references during insert. This allows to have dangling
	# references which are fixed later. The longest dangling
	# references are for the project level symbols, these we do
	# not save here, but at the end of the pass. What we need are
	# the ids, hence the two phases.

	struct::list assign [$self Active] revisions symbols
	foreach rev $revisions { $rev defid }
	foreach sym $symbols   { $sym defid }

	state transaction {
	    foreach rev $revisions { $rev persist }
	    foreach sym $symbols   { $sym persist }
	    $myblob persist
	}
	return
    }

    method drop {} {
	foreach {_ rev}    [array get myrev]      { $rev destroy }
	foreach {_ branch} [array get mybranches] { $branch destroy }
	foreach {_ taglist} [array get mytags] {
	    foreach tag $taglist { $tag destroy }
	}
	return
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

	$myblob add $revnr $myrev($revnr)

	if {$next ne ""} {
	    # parent revision NEXT is a delta of current.
	    $myblob delta $next $revnr
	}
	foreach b $branches {
	    # branch child revision B is a delta of current.
	    $myblob delta $b $revnr
	}

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
	# Note: We keep settext on file::rev for the hastext queries
	# used by several checks.
	$rev settext $textrange
	$rev setlod  $lod

	$myblob extend $revnr $textrange

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
	# and linking them into the trunk, possibly excluding
	# non-trunk data, and collecting aggregate symbol statistics.

	$self DetermineRevisionOperations
	$self DetermineLinesOfDevelopment
	$self HandleNonTrunkDefaultBranch
	$self RemoveIrrelevantDeletions
	$self RemoveInitialBranchDeletions

	if {[$myproject trunkonly]} {
	    $self ExcludeNonTrunkInformation
	}

	$self AggregateSymbolData
	return
    }

    # # ## ### ##### ######## #############
    ## Pass XII (Import).

    method pushto {repository} {
	log write 2 file {Importing file "$mypath"}

	set ws [$repository workspace]
	struct::list assign [$self Expand $ws] filemap revmap
	# filemap = dict (path -> uuid)
	# revmap  = dict (path -> rid)

	array set idmap [$repository importfiles $filemap]

	# Wipe workspace clean of the imported files.
	foreach x [glob -directory $ws r*] { ::file delete $x }

	foreach {path rid} $revmap {
	    set uuid $idmap($path)
	    state run {
		INSERT INTO revuuid (rid,  uuid)
		VALUES              ($rid, $uuid)
	    }
	}
	return
    }

    method Expand {dir} {
	set ex [struct::graph ex] ; # Expansion graph.
	set zp [struct::graph zp] ; # Zip/Import graph.

	close [open $dir/r__empty__ w];# Base for detached roots on branches.

	# Phase I: Pull blobs and referenced revisions from the state
	#          and fill the graphs with them...

	# Note: We use the blobs for expansion because we need them
	#       all, even those without revision, for both proper
	#       ordering and exact patch application.

	set earcs   {} ; # Arcs for expansion graph
	set zarcs   {} ; # Arcs for zip graph
	set revmap  {} ; # path -> rid map to later merge uuid information

	state foreachrow {
	    SELECT B.rid    AS xrid,
	           R.rev    AS revnr,
	           R.child  AS xchild,
	           B.coff   AS xcoff,
	           B.clen   AS xclen,
	           B.bid    AS cid,
	           B.pid    AS cparent
	    FROM            blob B
	    LEFT OUTER JOIN revision R
	    ON              B.rid = R.rid
	    WHERE  B.fid = $myid
	} {
	    # Main data are blobs, most will have revisions, but not
	    # all. The expansion graph is blob based, whereas the
	    # recompression graph is revision based.

	    if {$revnr ne ""} {
		# Blob has revision, extend recompression graph.

		lappend revmap r$revnr $xrid

		$zp node insert $xrid
		$zp node set    $xrid revnr $revnr
		$zp node set    $xrid label <$revnr>

		if {$xchild ne ""} {
		    lappend zarcs $xchild $xrid
		}
	    } else {
		# We fake a revnr for the blobs which have no
		# revision, for use in the expansion graph.
		set revnr ghost$cid
	    }

	    # Now the expansion graph.

	    $ex node insert $cid
	    $ex node set    $cid text  [list $xcoff $xclen]
	    $ex node set    $cid revnr $revnr
	    $ex node set    $cid label <$revnr>

	    if {$cparent ne ""} {
		# The expansion arcs go from baseline to delta
		# descendant, based on the blob information.
		lappend earcs $cparent $cid
	    }
	}

	# Phase II: Insert the accumulated dependencies

	foreach {from to} $earcs { $ex arc insert $from $to }
	foreach {from to} $zarcs { $zp arc insert $from $to }

	# Phase III: Traverse the graphs, expand the file, and
	#            generate import instructions.

	set archive [::file join [$myproject fullpath] $mypath]
	set ac      [open $archive r]
	fconfigure $ac -translation binary

	# First traverse the expansion graph, this gives us the
	# revisions in the order we have to expand them, which we do.

	set max [llength [$ex nodes]]
	set myimport 0

	gtcore datacmd   [mymethod ExpandData]
	gtcore formatcmd [mymethod ExpandFormat]
	gtcore sortcmd   [mymethod ExpandSort]
	gtcore savecmd   [mymethod Expand1 $ac $dir $max]

	gtcore traverse $ex {Expanding revisions...}
	close $ac

	# Now traverse the import graph, this builds the instruction
	# map for the fossil deltas.

	gtcore datacmd   [mymethod ExpandData]
	gtcore formatcmd [mymethod ExpandFormat]
	gtcore sortcmd   [mymethod ExpandSort]
	gtcore savecmd   [mymethod Expand2]

	set myimport {}
	gtcore traverse $zp {Generating revision order for import...}
	set filemap $myimport
	unset myimport

	# And back to import control

	$ex destroy
	$zp destroy

	return [list $filemap $revmap]
    }

    method ExpandData   {graph node} { return [$graph node get $node revnr] }
    method ExpandFormat {graph item} { return <[lindex $item 1]> } ; # revnr
    method ExpandSort   {graph candidates} {
	# candidates = list(item), item = list(node revnr)
	# Sort by node and revnr -> Trunk revisions come first.
	return [lsort -index 1 -dict [lsort -index 0 -dict $candidates]]
    }
    method Expand1 {chan dir max graph node} {
	log progress 3 file $myimport $max ; incr myimport

	set revnr           [$graph node get $node revnr]
	set fname          r$revnr
	struct::list assign [$graph node get $node text] offset length

	if {$length < 0} {
	    set data ""
	} else {
	    seek $chan $offset start
	    set data [string map {@@ @} [read $chan $length]]
	}

	if {![$graph node keyexists $node __base__]} {
	    # Full text node. Get the data, decode it, and save.

	    log write 8 file {Expanding <$revnr>, full text}

	    fileutil::writeFile -translation binary $dir/$fname $data
	} else {
	    # Delta node. __base__ is the name of the file containing
	    # the baseline. The patch is at the specified location of
	    # the archive file.

	    set fbase [$graph node get $node __base__]
	    log write 8 file {Expanding <$revnr>, is delta of <$fbase>}

	    set base [fileutil::cat -translation binary $dir/$fbase]

	    # Writing the patch to disk is just for better
	    # debugging. It is not used otherwise.
	    fileutil::writeFile $dir/rpatch $data
	    fileutil::writeFile -translation binary $dir/$fname \
		[Apply $base $data]
	}

	# Post to all successors that the just generated file is their
	# baseline.

	foreach out [$graph nodes -out $node] {
	    $graph node set $out __base__ $fname
	}
	return
    }

    proc Apply {base delta} {
	# base  = base text.
	# delta = delta in rcs format.
	#
	# Both strings are unencoded, i.e. things like @@, etc. have
	# already been replaced with their proper characters.
	#
	# Return value is the patched text.

	set base [split $base \n]
	set blen [llength $base]
	set ooff 0
	set res ""

	set lines  [split $delta \n]
	set nlines [llength $lines]

	log write 11 file {Base  lines = $blen}
	log write 11 file {Delta lines = $nlines}

	for {set i 0} {$i < $nlines} {} {
	    log write 11 file {    @ $i = [lindex $lines $i]}
	    log write 11 file { ooff $ooff}

	    if {![regexp {^([ad])(\d+)\s(\d+)$} [lindex $lines $i] -> cmd sl cn]} {
		trouble internal "Bad ed command '[lindex $lines $i]'"
	    }

	    log write 11 file {  cmd $cmd}
	    log write 11 file {   sl $sl}
	    log write 11 file {   cn $cn}

	    incr i
	    set el [expr {$sl + $cn}]

	    log write 11 file {   el $el}

	    switch -exact -- $cmd {
		d {
		    incr sl -1
		    incr el -1
		    if {$sl < $ooff} { trouble internal {Deletion before last edit} }
		    if {$sl > $blen} { trouble internal {Deletion past file end} }
		    if {$el > $blen} { trouble internal {Deletion beyond file end} }
		    foreach x [lrange $base $ooff [expr {$sl - 1}]] {
			log write 15 file {.|$x|}
			lappend res $x
		    }
		    set  ooff $el
		}
		a {
		    if {$sl < $ooff} { trouble internal {Insert before last edit} }
		    if {$sl > $blen} { trouble internal {Insert past file end} }

		    foreach x [lrange $base $ooff [expr {$sl - 1}]] {
			log write 15 file {.|$x|}
			lappend res $x
		    }
		    foreach x [lrange $lines $i [expr {$i + $cn - 1}]] {
			log write 15 file {+|$x|}
			lappend res $x
		    }
		    set ooff $sl
		    incr i $cn
		}
	    }
	}
	foreach x [lrange $base $ooff end] { lappend res $x }
	return [join $res \n]
    }

    method Expand2 {graph node} {
	set revnr [$graph node get $node revnr]

	# First import the file.
	lappend myimport [list A r$revnr {}]

	if {[$graph node keyexists $node __base__]} {
	    # Delta node. __base__ is the name of the file containing
	    # the baseline. Generate instruction to make the delta as
	    # well.

	    set fbase [$graph node get $node __base__]
	    lappend myimport [list D r$revnr r$fbase]
	}

	# Post to all successors that the just generated file is their
	# baseline. Exception: Those which ave already a baseline set.
	# Together with the sorting of trunk revisions first the trunk
	# should one uninterupted line, with branch roots _not_ delta
	# compressed per their branches.

	foreach out [$graph nodes -out $node] {
	    if {[$graph node keyexists $out __base__]} continue
	    $graph node set $out __base__ $revnr
	}
	return
    }

    variable myimport

    # # ## ### ##### ######## #############
    ## State

    variable myid              {} ; # File id in the persistent state.
    variable mypath            {} ; # Path of the file's rcs archive.
    variable myusrpath         {} ; # Path of the file as seen by users.
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

    variable myblob {} ; # Reference to the object managing the blob
			 # information (textrange of revisions, and
			 # delta dependencies) of this file.

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
        integrity assert {![rev istrunkrevnr $revnr]} {Expected a branch revision number}
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
	set branch [sym %AUTO% branch $branchnr [$myproject getsymbol $name] $self]
	$branch setposition [incr mybranchcnt]
	set mybranches($branchnr) $branch
	return $branch
    }

    method AddTag {name revnr} {
	set tag [sym %AUTO% tag $revnr [$myproject getsymbol $name] $self]
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

		if {[$branch haschildrev]} {
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
		    $rev addtag    $tag
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
	    integrity assert {$myroot eq ""} {Multiple root revisions found}
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

    # General note: In the following methods we only modify the links
    # between revisions and symbols to restructure the revision
    # tree. We do __not__ destroy the objects. Given the complex links
    # GC is difficult at this level. It is much easier to drop
    # everything when we we are done. This happens in 'drop', using
    # the state variable 'myrev', 'mybranches', and 'mytags'. What we
    # have to persist, performed by 'persist', we know will be
    # reachable through the revisions listed in 'myroots' and their
    # children and symbols.

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

	    $first cutfromparent ; # Sever revision from parent revision.
	    if {$stop ne ""} {
		$stop cutfromparent
		lappend myroots $stop ; # New root, after vendor branch
	    }

	    # Cut out the vendor branch symbol

	    set vendor [$first parentbranch]
	    integrity assert {$vendor ne ""} {First NTDB revision has no branch}
	    if {[$vendor parent] eq $rev11} {
		$rev11 removebranch        $vendor
		$rev11 removechildonbranch $first
		$vendor cutbranchparent    ;# bp = rev11, about to be gone
		$first cutfromparentbranch ;# pb = vendor, to be a detached LOD
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
	    $rev setondefaultbranch 1
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
	# From cvs2svn: If a file is added on a branch, then a trunk
	# revision is added at the same time in the 'Dead' state.
	# This revision doesn't do anything useful, so delete it.

	foreach root $myroots {
	    if {[$root isneeded]} continue
	    log write 2 file "Removing unnecessary dead revision [$root revnr]"

	    # Remove as root, make its child new root after
	    # disconnecting it from the revision just going away.

	    ldelete myroots $root
	    if {[$root haschild]} {
		set child [$root child]
		$child cutfromparent
		lappend myroots $child
	    }

	    # Cut out the branches spawned by the revision to be
	    # deleted. If the branch has revisions they should already
	    # use operation 'add', no need to change that. The first
	    # revision on each branch becomes a new and disconnected
	    # root.

	    foreach branch [$root branches] {
		$branch cutbranchparent
		if {![$branch haschild]} continue
		set first [$branch child]
		$first cutfromparent
		lappend myroots $first
	    }
	    $root removeallbranches

	    # Tagging a dead revision doesn't do anything, so remove
	    # any tags that were set on it.

	    $root removealltags

	    # This can only happen once per file, and we might have
	    # just changed myroots, so end the loop
	    break
	}
	return
    }

    method RemoveInitialBranchDeletions {} {
	# From cvs2svn: If the first revision on a branch is an
	# unnecessary delete, remove it.
	#
	# If a file is added on a branch (whether or not it already
	# existed on trunk), then new versions of CVS add a first
	# branch revision in the 'dead' state (to indicate that the
	# file did not exist on the branch when the branch was
	# created) followed by the second branch revision, which is an
	# add.  When we encounter this situation, we sever the branch
	# from trunk and delete the first branch revision.

	# At this point we may have already multiple roots in myroots,
	# we have to process them all.

	foreach root [$self LinesOfDevelopment] {
	    if {[$root isneededbranchdel]} continue
	    log write 2 file "Removing unnecessary initial branch delete [$root revnr]"

	    set branch [$root parentbranch]
	    set parent [$root parent]
	    set child  [$root child]

	    ldelete myroots $root
	    lappend myroots $child

	    $branch cutbranchparent
	    $branch cutchild
	    $child  cutfromparent

	    $branch setchild        $child
	    $child  setparentbranch $branch

	    $parent removebranch        $branch
	    $parent removechildonbranch $root
	}
	return
    }

    method LinesOfDevelopment {} {
	# Determine all lines of development for the file. This are
	# the known roots, and the root of all branches found on the
	# line of primary children.

	set lodroots {}
	foreach root $myroots {
	    $self AddBranchedLinesOfDevelopment lodroots $root
	    lappend lodroots $root
	}
	return $lodroots
    }

    method AddBranchedLinesOfDevelopment {lv root} {
	upvar 1 $lv lodroots
	while {$root ne ""} {
	    foreach branch [$root branches] {
		if {![$branch haschild]} continue
		set child [$branch child]
		# Recurse into the branch for deeper branches.
		$self AddBranchedLinesOfDevelopment lodroots $child
		lappend lodroots $child
	    }
	    set root [$root child]
	}
	return
    }

    method ExcludeNonTrunkInformation {} {
	# Remove all non-trunk branches, revisions, and tags. We do
	# keep the tags which are on the trunk.

	set ntdbroot ""
	foreach root [$self LinesOfDevelopment] {
	    # Note: Here the order of the roots is important,
	    # i.e. that we get them in depth first order. This ensures
	    # that the removal of a branch happens only after the
	    # branches spawned from it were removed. Otherwise the
	    # system might try to access deleted objects.

	    # Do not exclude the trunk.
	    if {[[$root lod] istrunk]} continue
	    $self ExcludeBranch $root ntdbroot
	}

	if {$ntdbroot ne ""} {
	    $self GraftNTDB2Trunk $ntdbroot
	}
	return
    }

    method ExcludeBranch {root nv} {
	# Exclude the branch/lod starting at root, a revision.
	#
	# If the LOD starts with non-trunk default branch revisions,
	# we leave them in place and do not delete the branch. In that
	# case the command sets the variable in NV so that we can
	# later rework these revisons to be purely trunk.

	if {[$root isondefaultbranch]} {
	    # Handling a NTDB. This branch may consists not only of
	    # NTDB revisions, but also some non-NTDB. The latter are
	    # truly on a branch and have to be excluded. The following
	    # loop determines if there are such revisions.

	    upvar 1 $nv ntdbroot
	    set ntdbroot $root
	    $root cutfromparentbranch

	    set rev $root
	    while {$rev ne ""} {
		$rev removeallbranches
		# See note [x].

		if {[$rev isondefaultbranch]} {
		    set rev [$rev child]
		} else {
		    break
		}
	    }

	    # rev now contains the first non-NTDB revision after the
	    # NTDB, or is empty if there is no such. If we have some
	    # they have to removed.

	    if {$rev ne ""}  {
		set lastntdb [$rev parent]
		$lastntdb cutfromchild
		while {$rev ne ""} {
		    $rev removealltags
		    $rev removeallbranches
		    # Note [x]: We may still have had branches on the
		    # revision. Branches without revisions committed
		    # on them do not show up in the list of roots aka
		    # lines of development.
		    set rev [$rev child]
		}
	    }
	    return
	}

	# No NTDB stuff to deal with. First delete the branch object
	# itself, after cutting all the various connections.

	set branch [$root parentbranch]
	if {$branch ne ""} {
	    set branchparent [$branch parent]
	    $branchparent removebranch        $branch
	    $branchparent removechildonbranch $root
	}

	# The root is no such any longer either.
	ldelete myroots $root

	# Now go through the line and remove all its revisions.

	while {$root ne ""} {
	    $root removealltags
	    $root removeallbranches
	    # Note: See the note [x].

	    # From cvs2svn: If this is the last default revision on a
	    # non-trunk default branch followed by a 1.2 revision,
	    # then the 1.2 revision depends on this one.  FIXME: It is
	    # questionable whether this handling is correct, since the
	    # non-trunk default branch revisions affect trunk and
	    # should therefore not just be discarded even if
	    # --trunk-only.

	    if {[$root hasdefaultbranchchild]} {
		set ntdbchild [$root defaultbranchchild]
		integrity assert {
		    [$ntdbchild defaultbranchparent] eq $ntdbchild
		} {ntdb - trunk linkage broken}
		$ntdbchild cutdefaultbranchparent
		if {[$ntdbchild hasparent]} {
		    lappend myroots [$ntdbchild parent]
		}
	    }

	    set root [$root child]
	}

	return
    }

    method GraftNTDB2Trunk {root} {
	# We can now graft the non-trunk default branch revisions to
	# trunk. They should already be alone on a CVSBranch-less
	# branch.

	integrity assert {![$root hasparentbranch]} {NTDB root still has its branch symbol}
	integrity assert {![$root hasbranches]}     {NTDB root still has spawned branches}

	set last $root
	while {[$last haschild]} {set last [$last child]}

	if {[$last hasdefaultbranchchild]} {

	    set rev12 [$last defaultbranchchild]
	    $rev12 cutdefaultbranchparent
	    $last  cutdefaultbranchchild

	    $rev12 changeparent $last
	    $last  changechild $rev12

	    ldelete myroots $rev12

	    # Note and remember that the type of rev12 was already
	    # adjusted by AdjustNonTrunkDefaultBranch, so we don't
	    # have to change its type here.
	}

	while {$root ne ""} {
	    $root setondefaultbranch 0
	    $root setlod $mytrunk
	    foreach tag [$root tags] {
		$tag setlod $mytrunk
	    }
	    set root [$root child]
	}

        return
    }

    method Active {} {
	set revisions {}
	set symbols   {}

	foreach root [$self LinesOfDevelopment] {
	    if {[$root hasparentbranch]} { lappend symbols [$root parentbranch] }
	    while {$root ne ""} {
		lappend revisions $root
		foreach tag    [$root tags]     { lappend symbols $tag    }
		foreach branch [$root branches] {
		    integrity assert {
			[$branch parent] eq $root
		    } {Backreference branch to its root is missing or wrong}
		    lappend symbols $branch
		}
		set lod [$root lod]
		if {![$lod istrunk]} {
		    integrity assert {
			[$lod haschild]
		    } {Branch is LOD symbol without revisions}
		    lappend symbols $lod
		}
		set root [$root child]
	    }
	}

	return [list [lsort -unique -dict $revisions] [lsort -unique -dict $symbols]]
    }


    method AggregateSymbolData {} {
	# Now that the exact set of revisions (and through that
	# branches and tags) is known we can update the aggregate
	# symbol statistics.

	foreach root [$self LinesOfDevelopment] {
	    set lod [$root lod]

	    # Note: If the LOD is the trunk the count*, etc. methods
	    # will do nothing, as it is always present (cannot be
	    # excluded), and is always a branch too.

	    # Lines of development count as branches and have a commit
	    # on them (root). If they are still attached to a tree we
	    # have to compute and register possible parents.

	    $lod countasbranch
	    $lod countacommit

	    if {[$root hasparentbranch]} {
		# Note lod == [$root parentbranch]
		$lod possibleparents
	    } elseif {![$lod istrunk] && [$root isondefaultbranch]} {
		# This is the root revision of a detached NTDB. We
		# have to manually set the only possible parent for
		# this LOD, the trunk itself.

		[$lod symbol] possibleparent $mytrunk
	    }

	    # For the revisions in the line we register their branches
	    # and tags as blockers for the lod, and update the type
	    # counters as well. As branch symbols without commits on
	    # them are not listed as lines of development, we have to
	    # count them here as well, as plain branches. At last we
	    # have to compute and register the possible parents of the
	    # tags, in case they are later converted as branches.

	    while {$root ne ""} {
		foreach branch [$root branches] {
		    $lod blockedby $branch
		    $branch possibleparents
		    if {[$branch haschild]} continue
		    $branch countasbranch
		}

		foreach tag [$root tags] {
		    $lod blockedby $tag
		    $tag possibleparents
		    $tag countastag
		}

		set root [$root child]
	    }
	}

	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection
    pragma -hastypemethods no  ; # type is not relevant.

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
	namespace import ::vc::fossil::import::cvs::blobstore
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::fossil::import::cvs::gtcore
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::file 1.0
return
