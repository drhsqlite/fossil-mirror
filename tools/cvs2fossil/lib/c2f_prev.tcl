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

## Revisions per project, aka Changesets. These objects are first used
## in pass 5, which creates the initial set covering the repository.

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                               ; # Required runtime.
package require snit                                  ; # OO system.
package require struct::set                           ; # Set operations.
package require vc::tools::misc                       ; # Text formatting
package require vc::tools::trouble                    ; # Error reporting.
package require vc::tools::log                        ; # User feedback.
package require vc::fossil::import::cvs::state        ; # State storage.
package require vc::fossil::import::cvs::integrity    ; # State integrity checks.

# # ## ### ##### ######## ############# #####################
##

snit::type ::vc::fossil::import::cvs::project::rev {
    # # ## ### ##### ######## #############
    ## Public API

    constructor {project cstype srcid items {theid {}}} {
	if {$theid ne ""} {
	    set myid $theid
	} else {
	    set myid [incr mycounter]
	}

	integrity assert {
	    [info exists mycstype($cstype)]
	} {Bad changeset type '$cstype'.}

	set myproject   $project
	set mytype      $cstype
	set mytypeobj   ::vc::fossil::import::cvs::project::rev::${cstype}
	set mysrcid	$srcid
	set myitems     $items
	set mypos       {} ; # Commit location is not known yet.

	foreach iid $items { lappend mytitems [list $cstype $iid] }

	# Keep track of the generated changesets and of the inverse
	# mapping from items to them.
	lappend mychangesets   $self
	lappend mytchangesets($cstype) $self
	set     myidmap($myid) $self

	MapItems $cstype $items
	return
    }

    destructor {
	# We may be able to get rid of this entirely, at least for
	# (de)construction and pass InitCSets.

	UnmapItems $mytype $myitems
	unset myidmap($myid)

	set pos                    [lsearch -exact $mychangesets $self]
	set mychangesets           [lreplace       $mychangesets $pos $pos]
	set pos                    [lsearch -exact $mytchangesets($mytype) $self]
	set mytchangesets($mytype) [lreplace       $mytchangesets($mytype) $pos $pos]
	return
    }

    method str {} {
	set str    "<"
	set detail ""
	if {[$mytypeobj bysymbol]} {
	    set detail " '[state one {
		SELECT S.name
		FROM   symbol S
		WHERE  S.sid = $mysrcid
	    }]'"
	}
	append str "$mytype ${myid}${detail}>"
	return $str
    }

    method lod {} {
	return [$mytypeobj cs_lod $mysrcid $myitems]
    }

    method id    {} { return $myid }
    method items {} { return $mytitems }
    method data  {} { return [list $myproject $mytype $mysrcid] }

    delegate method bysymbol   to mytypeobj
    delegate method byrevision to mytypeobj
    delegate method isbranch   to mytypeobj
    delegate method istag      to mytypeobj

    method setpos {p} { set mypos $p ; return }
    method pos    {}  { return $mypos }

    method determinesuccessors {} {
	# Pass 6 operation. Compute project-level dependencies from
	# the file-level data and save it back to the state. This may
	# be called during the cycle breaker passes as well, to adjust
	# the successor information of changesets which are the
	# predecessors of dropped changesets. For them we have to
	# remove their existing information first before inserting the
	# new data.
	state run {
	    DELETE FROM cssuccessor WHERE cid = $myid;
	}
	set loop 0
	# TODO: Check other uses of cs_sucessors.
	# TODO: Consider merging cs_sucessor's SELECT with the INSERT here.
	foreach nid [$mytypeobj cs_successors $myitems] {
	    state run {
		INSERT INTO cssuccessor (cid,  nid)
		VALUES                  ($myid,$nid)
	    }
	    if {$nid == $myid} { set loop 1 }
	}
	# Report after the complete structure has been saved.
	if {$loop} { $self reportloop }
	return
    }

    # result = list (changeset)
    method successors {} {
	# Use the data saved by pass 6.
	return [struct::list map [state run {
	    SELECT S.nid
	    FROM   cssuccessor S
	    WHERE  S.cid = $myid
	}] [mytypemethod of]]
    }

    # item -> list (item)
    method nextmap {} {
	$mytypeobj successors tmp $myitems
	return [array get tmp]
    }

    method breakinternaldependencies {cv} {
	upvar 1 $cv counter
	log write 14 csets {[$self str] BID}
	vc::tools::mem::mark

	# This method inspects the changeset, looking for internal
	# dependencies. Nothing is done if there are no such.

	# Otherwise the changeset is split into a set of fragments
	# which have no internal dependencies, transforming the
	# internal dependencies into external ones. The new changesets
	# generated from the fragment information are added to the
	# list of all changesets (by the caller).

	# The code checks only successor dependencies, as this auto-
	# matically covers the predecessor dependencies as well (Any
	# successor dependency a -> b is also a predecessor dependency
	# b -> a).

	array set breaks {}

	set fragments [BreakDirectDependencies $myitems breaks]

	if {![llength $fragments]} { return {} }

	return [$self CreateFromFragments $fragments counter breaks]
    }

    method persist {} {
	set tid $mycstype($mytype)
	set pid [$myproject id]
	set pos 0

	state transaction {
	    state run {
		INSERT INTO changeset (cid,   pid,  type, src)
		VALUES                ($myid, $pid, $tid, $mysrcid);
	    }

	    foreach iid $myitems {
		state run {
		    INSERT INTO csitem (cid,   pos,  iid)
		    VALUES             ($myid, $pos, $iid);
		}
		incr pos
	    }
	}
	return
    }

    method timerange {} { return [$mytypeobj timerange $myitems] }

    method limits {} {
	struct::list assign [$mytypeobj limits $myitems] maxp mins
	return [list [TagItemDict $maxp $mytype] [TagItemDict $mins $mytype]]
    }

    method drop {} {
	log write 8 csets {Dropping $self = [$self str]}

	state transaction {
	    state run {
		DELETE FROM changeset   WHERE cid = $myid;
		DELETE FROM csitem      WHERE cid = $myid;
		DELETE FROM cssuccessor WHERE cid = $myid;
	    }
	}

	# Return the list of predecessors so that they can be adjusted.
	return [struct::list map [state run {
	    SELECT cid
	    FROM   cssuccessor
	    WHERE  nid = $myid
	}] [mytypemethod of]]
    }

    method reportloop {{kill 1}} {
	# We print the items which are producing the loop, and how.

	set hdr "Self-referential changeset [$self str] __________________"
	set ftr [regsub -all {[^ 	]} $hdr {_}]

	log write 0 csets $hdr
	foreach {item nextitem} [$mytypeobj loops $myitems] {
	    # Create tagged items from the id and our type.
	    set item     [list $mytype  $item]
	    set nextitem [list $mytype $nextitem]
	    # Printable labels.
	    set i  "<[$type itemstr $item]>"
	    set n  "<[$type itemstr $nextitem]>"
	    set ncs $myitemmap($nextitem)
	    # Print
	    log write 0 csets {* $i --> $n --> cs [$ncs str]}
	}
	log write 0 csets $ftr

	if {!$kill} return
	trouble internal "[$self str] depends on itself"
	return
    }

    method pushto {repository date rstate} {
	# Generate and import the manifest for this changeset.
	#
	# Data needed:
	# - Commit message               (-- mysrcid -> repository meta)
	# - User doing the commit        (s.a.)
	#
	# - Timestamp of when committed  (command argument)
	#
	# - The parent changeset, if any. If there is no parent fossil
	#   will use the empty base revision as parent.
	#
	# - List of the file revisions in the changeset.

	# We derive the lod information directly from the revisions of
	# the changeset, as the branch part of the meta data (s.a.) is
	# outdated since pass FilterSymbols. See the method 'run' in
	# file "c2f_pfiltersym.tcl" for more commentary on this.

	set lodname [$self lod]

	log write 2 csets {Importing changeset [$self str] on $lodname}

	if {[$mytypeobj istag]} {
	    # Handle tags. They appear immediately after the revision
	    # they are attached to (*). We can assume that the
	    # workspace for the relevant line of development
	    # exists. We retrieve it, then the uuid of the last
	    # revision entered into it, then tag this revision.

	    # (*) Immediately in terms of the relevant line of
	    #     development. Revisions on other lines may come in
	    #     between, but they do not matter to that.

	    set lws  [Getworkspace $rstate $lodname $myproject 0]
	    set uuid [lindex [$lws getid] 1]

	    $repository tag $uuid [state one {
		SELECT S.name
		FROM   symbol S
		WHERE  S.sid = $mysrcid
	    }]

	} elseif {[$mytypeobj isbranch]} {

	    # Handle branches. They appear immediately after the
	    # revision they are spawned from (*). We can assume that
	    # the workspace for the relevant line of development
	    # exists.

	    # We retrieve it, then the uuid of the last revision
	    # entered into it. That revision is tagged as the root of
	    # the branch (**). A new workspace for the branch is
	    # created as well, for the future revisions of the new
	    # line of development.

	    # An exception is made of the non-trunk default branch,
	    # aka vendor branch. This lod has to have a workspace not
	    # inherited from anything else. It has no root either, so
	    # tagging is out as well.

	    # (*) Immediately in terms of the relevant line of
	    #     development. Revisions on other lines may come in
	    #     between, but they do not matter to that.

	    # (**) Tagging the parent revision of the branch as its
	    #      root is done to let us know about the existence of
	    #      the branch even if it has no revisions committed to
	    #      it, and thus no regular branch tag anywhere else.
	    #      The name of the tag is the name for the lod, with
	    #      the suffix '-root' appended to it.

	    # LOD is self symbol of branch, not parent
	    set lodname [state one {
		SELECT S.name
		FROM   symbol S
		WHERE  S.sid = $mysrcid
	    }]

	    if {![$rstate has :trunk:]} {
		# No trunk implies default branch. Just create the
		# proper workspace.
		Getworkspace $rstate $lodname $myproject 1
	    } else {
		# Non-default branch. Need workspace, and tag parent
		# revision.

		set lws [Getworkspace $rstate $lodname $myproject 0]
		set uuid [lindex [$lws getid] 1]

		$repository tag $uuid ${lodname}-root
	    }
	} else {
	    # Revision changeset.

	    struct::list assign [$myproject getmeta $mysrcid] __ __ user message

	    # Perform the import. As part of that we determine the
	    # parent we need, and convert the list of items in the
	    # changeset into uuids and printable data.

	    struct::list assign [Getisdefault $myitems] \
		isdefault lastdefaultontrunk

	    log write 8 csets {LOD    '$lodname'}
	    log write 8 csets { def?  $isdefault}
	    log write 8 csets { last? $lastdefaultontrunk}

	    set lws  [Getworkspace    $rstate $lodname $myproject $isdefault]
	    $lws add [Getrevisioninfo $myitems]

	    struct::list assign \
		[$repository importrevision [$self str] \
		     $user $message $date \
		     [lindex [$lws getid] 0] [$lws get]] \
		rid uuid

	    if {[$lws ticks] == 1} {
		# First commit on this line of development. Set our
		# own name as a propagating tag. And if the LOD has a
		# parent we have to prevent the propagation of that
		# tag into this new line.

		set plws [$lws parent]
		if {$plws ne ""} {
		    $repository branchcancel $uuid [$plws name]
		}
		$repository branchmark $uuid [$lws name]
	    }

	    # Remember the imported changeset in the state, under our
	    # LOD. And if it is the last trunk changeset on the vendor
	    # branch then the revision is also the actual root of the
	    # :trunk:, so we remember it as such in the state. However
	    # if the trunk already exists then the changeset cannot be
	    # on it any more. This indicates weirdness in the setup of
	    # the vendor branch, but one we can work around.

	    $lws defid [list $rid $uuid]
	    if {$lastdefaultontrunk} {
		log write 2 csets {This cset is the last on the NTDB, set the trunk workspace up}

		if {[$rstate has :trunk:]} {
		    log write 2 csets {Multiple changesets declared to be the last trunk changeset on the vendor-branch}
		} else {
		    $rstate new :trunk: [$lws name]
		}
	    }
	}

	log write 2 csets { }
	log write 2 csets { }
	return
    }

    proc Getrevisioninfo {revisions} {
	set theset ('[join $revisions {','}]')
	set revisions {}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT U.uuid    AS frid,
	           F.visible AS path,
	           F.name    AS fname,
	           R.rev     AS revnr,
	           R.op      AS rop
	    FROM   revision R, revuuid U, file F
	    WHERE  R.rid IN $theset  -- All specified revisions
	    AND    U.rid = R.rid     -- get fossil uuid of revision
	    AND    F.fid = R.fid     -- get file of revision
	}] {
	    lappend revisions $frid $path $fname/$revnr $rop
	}
	return $revisions
    }

    proc Getworkspace {rstate lodname project isdefault} {

	# The state object holds the workspace state of each known
	# line-of-development (LOD), up to the last committed
	# changeset belonging to that LOD.

	# (*) Standard handling if in-LOD changesets. If the LOD of
	#     the current changeset exists in the state (= has been
	#     committed to) then this it has the workspace we are
	#     looking for.

	if {[$rstate has $lodname]} {
	    return [$rstate get $lodname]
	}

	# If the LOD is however not yet known, then the current
	# changeset can be either of
	# (a) root of a vendor branch,
	# (b) root of the trunk LOD, or
	# (c) the first changeset in a new LOD which was spawned from
	#     an existing LOD.

	# For both (a) and (b) we have to create a new workspace for
	# the lod, and it doesn't inherit from anything.

	# One exception for (a). If we already have a :vendor: branch
	# then multiple symbols were used for the vendor branch by
	# different files. In that case the 'new' branch is made an
	# alias of the :vendor:, effectively merging the symbols
	# together.

	# Note that case (b) may never occur. See the variable
	# 'lastdefaultontrunk' in the caller (method pushto). This
	# flag can the generation of the workspace for the :trunk: LOD
	# as well, making it inherit the state of the last
	# trunk-changeset on the vendor-branch.

	if {$isdefault} {
	    if {![$rstate has ":vendor:"]} {
		# Create the vendor branch if not present already. We
		# use the actual name for the lod, and additional make
		# it accessible under an internal name (:vendor:) so
		# that we can merge to it later, should it become
		# necessary. See the other branch below.
		$rstate new $lodname
		$rstate dup :vendor: <-- $lodname
	    } else {
		# Merge the new symbol to the vendor branch
		$rstate dup $lodname <-- :vendor:
	    }
	    return [$rstate get $lodname]
	}

	if {$lodname eq ":trunk:"} {
	    return [$rstate new $lodname]
	}

	# Case (c). We find the parent LOD of our LOD and let the new
	# workspace inherit from the parent's workspace.

	set plodname [[[$project getsymbol $lodname] parent] name]

	log write 8 csets {pLOD   '$plodname'}

	if {[$rstate has $plodname]} {
	    return [$rstate new $lodname $plodname]
	}

	foreach k [lsort [$rstate names]] {
	    log write 8 csets {    $k = [[$rstate get $k] getid]}
	}

	trouble internal {Unable to determine changeset parent}
	return
    }

    proc Getisdefault {revisions} {
	set theset ('[join $revisions {','}]')

	struct::list assign [state run [subst -nocommands -nobackslashes {
	    SELECT R.isdefault, R.dbchild
	    FROM   revision R
	    WHERE  R.rid IN $theset  -- All specified revisions
	    LIMIT 1
	}]] def last

	# TODO/CHECK: look for changesets where isdefault/dbchild is
	# ambigous.

	return [list $def [expr {$last ne ""}]]
    }

    typemethod split {cset args} {
	# As part of the creation of the new changesets specified in
	# ARGS as sets of items, all subsets of CSET's item set, CSET
	# will be dropped from all databases, in and out of memory,
	# and then destroyed.
	#
	# Note: The item lists found in args are tagged items. They
	# have to have the same type as the changeset, being subsets
	# of its items. This is checked in Untag1.

	log write 8 csets {OLD: [lsort [$cset items]]}
	ValidateFragments $cset $args

	# All checks pass, actually perform the split.

	struct::list assign [$cset data] project cstype cssrc

	set predecessors [$cset drop]
	$cset destroy

	set newcsets {}
	foreach fragmentitems $args {
	    log write 8 csets {MAKE: [lsort $fragmentitems]}

	    set fragment [$type %AUTO% $project $cstype $cssrc \
			      [Untag $fragmentitems $cstype]]
	    lappend newcsets $fragment

	    $fragment persist
	    $fragment determinesuccessors
	}

	# The predecessors have to recompute their successors, i.e.
	# remove the dropped changeset and put one of the fragments
	# into its place.
	foreach p $predecessors {
	    $p determinesuccessors
	}

	return $newcsets
    }

    typemethod itemstr {item} {
	struct::list assign $item itype iid
	return [$itype str $iid]
    }

    typemethod strlist {changesets} {
	return [join [struct::list map $changesets [myproc ID]]]
    }

    proc ID {cset} { $cset str }

    proc Untag {taggeditems cstype} {
	return [struct::list map $taggeditems [myproc Untag1 $cstype]]
    }

    proc Untag1 {cstype theitem} {
	struct::list assign $theitem t i
	integrity assert {$cstype eq $t} {Item $i's type is '$t', expected '$cstype'}
	return $i
    }

    proc TagItemDict {itemdict cstype} {
	set res {}
	foreach {i v} $itemdict { lappend res [list $cstype $i] $v }
	return $res
    }

    proc ValidateFragments {cset fragments} {
	# Check the various integrity constraints for the fragments
	# specifying how to split the changeset:
	#
	# * We must have two or more fragments, as splitting a
	#   changeset into one makes no sense.
	# * No fragment may be empty.
	# * All fragments have to be true subsets of the items in the
	#   changeset to split. The 'true' is implied because none are
	#   allowed to be empty, so each has to be smaller than the
	#   total.
	# * The union of the fragments has to be the item set of the
	#   changeset.
	# * The fragment must not overlap, i.e. their pairwise
	#   intersections have to be empty.

	set cover {}
	foreach fragmentitems $fragments {
	    log write 8 csets {NEW: [lsort $fragmentitems]}

	    integrity assert {
		![struct::set empty $fragmentitems]
	    } {changeset fragment is empty}

	    integrity assert {
		[struct::set subsetof $fragmentitems [$cset items]]
	    } {changeset fragment is not a subset}
	    struct::set add cover $fragmentitems
	}

	integrity assert {
	    [struct::set equal $cover [$cset items]]
	 } {The fragments do not cover the original changeset}

	set i 1
	foreach fia $fragments {
	    foreach fib [lrange $fragments $i end] {
		integrity assert {
		    [struct::set empty [struct::set intersect $fia $fib]]
		} {The fragments <$fia> and <$fib> overlap}
	    }
	    incr i
	}

	return
    }

    # # ## ### ##### ######## #############
    ## State

    variable myid        {} ; # Id of the cset for the persistent
			      # state.
    variable myproject   {} ; # Reference of the project object the
			      # changeset belongs to.
    variable mytype      {} ; # What the changeset is based on
			      # (revisions, tags, or branches).
			      # Values: See mycstype. Note that we
			      # have to keep the names of the helper
			      # singletons in sync with the contents
			      # of state table 'cstype', and various
			      # other places using them hardwired.
    variable mytypeobj   {} ; # Reference to the container for the
			      # type dependent code. Derived from
			      # mytype.
    variable mysrcid     {} ; # Id of the metadata or symbol the cset
			      # is based on.
    variable myitems     {} ; # List of the file level revisions,
			      # tags, or branches in the cset, as
			      # ids. Not tagged.
    variable mytitems    {} ; # As myitems, the tagged form.
    variable mypos       {} ; # Commit position of the changeset, if
			      # known.

    # # ## ### ##### ######## #############
    ## Internal methods

    typevariable mycounter        0 ; # Id counter for csets. Last id
				      # used.
    typevariable mycstype -array {} ; # Map cstypes (names) to persistent
				      # ids. Note that we have to keep
				      # the names in the table 'cstype'
				      # in sync with the names of the
				      # helper singletons.

    typemethod inorder {projectid} {
	# Return all changesets (object references) for the specified
	# project, in the order given to them by the sort passes. Both
	# the filtering by project and the sorting by time make the
	# use of 'project::rev rev' impossible.

	set res {}
	state foreachrow {
	    SELECT C.cid  AS xcid,
	           T.date AS cdate
	    FROM   changeset C, cstimestamp T
	    WHERE  C.pid  = $projectid -- limit to changesets in project
	    AND    T.cid  = C.cid      -- get ordering information
	    ORDER BY T.date            -- sort into commit order
	} {
	    lappend res $myidmap($xcid) $cdate
	}
	return $res
    }

    typemethod getcstypes {} {
	state foreachrow {
	    SELECT tid, name FROM cstype;
	} { set mycstype($name) $tid }
	return
    }

    typemethod load {repository} {
	set n 0
	log write 2 csets {Loading the changesets}
	state foreachrow {
	    SELECT C.cid   AS id,
	           C.pid   AS xpid,
                   CS.name AS cstype,
	           C.src   AS srcid
	    FROM   changeset C, cstype CS
	    WHERE  C.type = CS.tid
	    ORDER BY C.cid
	} {
	    log progress 2 csets $n {}
	    set r [$type %AUTO% [$repository projectof $xpid] $cstype $srcid [state run {
		SELECT C.iid
		FROM   csitem C
		WHERE  C.cid = $id
		ORDER BY C.pos
	    }] $id]
	    incr n
	}
	return
    }

    typemethod loadcounter {} {
	# Initialize the counter from the state
	log write 2 csets {Loading changeset counter}
	set mycounter [state one { SELECT MAX(cid) FROM changeset }]
	return
    }

    typemethod num {} { return $mycounter }

    # # ## ### ##### ######## #############

    method CreateFromFragments {fragments cv bv} {
	upvar 1 $cv counter $bv breaks
	UnmapItems $mytype $myitems

	# Create changesets for the fragments, reusing the current one
	# for the first fragment. We sort them in order to allow
	# checking for gaps and nice messages.

	set newcsets  {}
	set fragments [lsort -index 0 -integer $fragments]

	#puts \t.[join [PRs $fragments] .\n\t.].

	Border [lindex $fragments 0] firsts firste

	integrity assert {
	    $firsts == 0
	} {Bad fragment start @ $firsts, gap, or before beginning of the range}

	set laste $firste
	foreach fragment [lrange $fragments 1 end] {
	    Border $fragment s e
	    integrity assert {
		$laste == ($s - 1)
	    } {Bad fragment border <$laste | $s>, gap or overlap}

	    set new [$type %AUTO% $myproject $mytype $mysrcid [lrange $myitems $s $e]]
	    lappend newcsets $new
	    incr counter

            log write 4 csets {Breaking [$self str ] @ $laste, new [$new str], cutting $breaks($laste)}

	    set laste $e
	}

	integrity assert {
	    $laste == ([llength $myitems]-1)
	} {Bad fragment end @ $laste, gap, or beyond end of the range}

	# Put the first fragment into the current changeset, and
	# update the in-memory index. We can simply (re)add the items
	# because we cleared the previously existing information, see
	# 'UnmapItems' above. Persistence does not matter here, none
	# of the changesets has been saved to the persistent state
	# yet.

	set myitems  [lrange $myitems  0 $firste]
	set mytitems [lrange $mytitems 0 $firste]
	MapItems $mytype $myitems
	return $newcsets
    }

    # # ## ### ##### ######## #############

    proc BreakDirectDependencies {theitems bv} {
	upvar 1 mytypeobj mytypeobj self self $bv breaks

	# Array of dependencies (parent -> child). This is pulled from
	# the state, and limited to successors within the changeset.

	array set dependencies {}

	$mytypeobj internalsuccessors dependencies $theitems
	if {![array size dependencies]} {
	    return {}
	} ; # Nothing to break.

	log write 5 csets ...[$self str].......................................................
	vc::tools::mem::mark

	return [BreakerCore $theitems dependencies breaks]
    }

    proc BreakerCore {theitems dv bv} {
	# Break a set of revisions into fragments which have no
	# internal dependencies.

	# We perform all necessary splits in one go, instead of only
	# one. The previous algorithm, adapted from cvs2svn, computed
	# a lot of state which was thrown away and then computed again
	# for each of the fragments. It should be easier to update and
	# reuse that state.

	upvar 1 $dv dependencies $bv breaks

	# We have internal dependencies to break. We now iterate over
	# all positions in the list (which is chronological, at least
	# as far as the timestamps are correct and unique) and
	# determine the best position for the break, by trying to
	# break as many dependencies as possible in one go. When a
	# break was found this is redone for the fragments coming and
	# after, after upding the crossing information.

	# Data structures:
	# Map:  POS   revision id      -> position in list.
	#       CROSS position in list -> number of dependencies crossing it
	#       DEPC  dependency       -> positions it crosses
	# List: RANGE Of the positions itself.
	# Map:  DELTA position in list -> time delta between its revision
	#                                 and the next, if any.
	# A dependency is a single-element map parent -> child

	# InitializeBreakState initializes their contents after
	# upvar'ing them from this scope. It uses the information in
	# DEPENDENCIES to do so.

	InitializeBreakState $theitems

	set fragments {}
	set new       [list $range]

	# Instead of one list holding both processed and pending
	# fragments we use two, one for the framents to process, one
	# to hold the new fragments, and the latter is copied to the
	# former when they run out. This keeps the list of pending
	# fragments short without sacrificing speed by shifting stuff
	# down. We especially drop the memory of fragments broken
	# during processing after a short time, instead of letting it
	# consume memory.

	while {[llength $new]} {

	    set pending $new
	    set new     {}
	    set at      0

	    while {$at < [llength $pending]} {
		set current [lindex $pending $at]

		log write 6 csets {. . .. ... ..... ........ .............}
		log write 6 csets {Scheduled   [join [PRs [lrange $pending $at end]] { }]}
		log write 6 csets {Considering [PR $current] \[$at/[llength $pending]\]}

		set best [FindBestBreak $current]

		if {$best < 0} {
		    # The inspected range has no internal
		    # dependencies. This is a complete fragment.
		    lappend fragments $current

		    log write 6 csets "No breaks, final"
		} else {
		    # Split the range and schedule the resulting
		    # fragments for further inspection. Remember the
		    # number of dependencies cut before we remove them
		    # from consideration, for documentation later.

		    set breaks($best) $cross($best)

		    log write 6 csets "Best break @ $best, cutting [nsp $cross($best) dependency dependencies]"

		    # Note: The value of best is an abolute location
		    # in myitems. Use the start of current to make it
		    # an index absolute to current.

		    set brel [expr {$best - [lindex $current 0]}]
		    set bnext $brel ; incr bnext
		    set fragbefore [lrange $current 0 $brel]
		    set fragafter  [lrange $current $bnext end]

		    log write 6 csets "New pieces  [PR $fragbefore] [PR $fragafter]"

		    integrity assert {[llength $fragbefore]} {Found zero-length fragment at the beginning}
		    integrity assert {[llength $fragafter]}  {Found zero-length fragment at the end}

		    lappend new $fragbefore $fragafter
		    CutAt $best
		}

		incr at
	    }
	}

	log write 6 csets ". . .. ... ..... ........ ............."

	return $fragments
    }

    proc InitializeBreakState {revisions} {
	upvar 1 pos pos cross cross range range depc depc delta delta \
	    dependencies dependencies

	# First we create a map of positions to make it easier to
	# determine whether a dependency crosses a particular index.

	log write 14 csets {IBS: #rev [llength $revisions]}
	log write 14 csets {IBS: pos map, cross counter}

	array set pos   {}
	array set cross {}
	array set depc  {}
	set range       {}
	set n 0
	foreach rev $revisions {
	    lappend range $n
	    set pos($rev) $n
	    set cross($n) 0
	    incr n
	}

	log write 14 csets {IBS: pos/[array size pos], cross/[array size cross]}

	# Secondly we count the crossings per position, by iterating
	# over the recorded internal dependencies.

	# Note: If the timestamps are badly out of order it is
	#       possible to have a backward successor dependency,
	#       i.e. with start > end. We may have to swap the indices
	#       to ensure that the following loop runs correctly.
	#
	# Note 2: start == end is not possible. It indicates a
	#         self-dependency due to the uniqueness of positions,
	#         and that is something we have ruled out already, see
	#         'rev internalsuccessors'.

	log write 14 csets {IBS: cross counter filling, pos/cross map}

	foreach {rid children} [array get dependencies] {
	    foreach child $children {
		set dkey    [list $rid $child]
		set start   $pos($rid)
		set end     $pos($child)

		if {$start > $end} {
		    set crosses [list $end [expr {$start-1}]]
		    while {$end < $start} {
			incr cross($end)
			incr end
		    }
		} else {
		    set crosses [list $start [expr {$end-1}]]
		    while {$start < $end} {
			incr cross($start)
			incr start
		    }
		}
		set depc($dkey) $crosses
	    }
	}

	log write 14 csets {IBS: pos/[array size pos], cross/[array size cross], depc/[array size depc] (for [llength $revisions])}
	log write 14 csets {IBS: timestamps, deltas}

	InitializeDeltas $revisions

	log write 14 csets {IBS: delta [array size delta]}
	return
    }

    proc InitializeDeltas {revisions} {
	upvar 1 delta delta

	# Pull the timestamps for all revisions in the changesets and
	# compute their deltas for use by the break finder.

	array set delta {}
	array set stamp {}

	set theset ('[join $revisions {','}]')
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT R.rid AS xrid, R.date AS time
	    FROM revision R
	    WHERE R.rid IN $theset
	}] {
	    set stamp($xrid) $time
	}

	log write 14 csets {IBS: stamp [array size stamp]}

	set n 0
	foreach rid [lrange $revisions 0 end-1] rnext [lrange $revisions 1 end] {
	    set delta($n) [expr {$stamp($rnext) - $stamp($rid)}]
	    incr n
	}
	return
    }

    proc FindBestBreak {range} {
	upvar 1 cross cross delta delta

	# Determine the best break location in the given range of
	# positions. First we look for the locations with the maximal
	# number of crossings. If there are several we look for the
	# shortest time interval among them. If we still have multiple
	# possibilities after that we select the earliest location
	# among these.

	# Note: If the maximal number of crossings is 0 then the range
	#       has no internal dependencies, and no break location at
	#       all. This possibility is signaled via result -1.

	# Note: A range of length 1 or less cannot have internal
	#       dependencies, as that needs at least two revisions in
	#       the range.

	if {[llength $range] < 2} { return -1 }

	set max -1
	set best {}

	foreach location $range {
	    set crossings $cross($location)
	    if {$crossings > $max} {
		set max  $crossings
		set best [list $location]
		continue
	    } elseif {$crossings == $max} {
		lappend best $location
	    }
	}

	if {$max == 0}            { return -1 }
	if {[llength $best] == 1} { return [lindex $best 0] }

	set locations $best
	set best {}
	set min -1

	foreach location $locations {
	    set interval $delta($location)
	    if {($min < 0) || ($interval < $min)} {
		set min  $interval
		set best [list $location]
	    } elseif {$interval == $min} {
		lappend best $location
	    }
	}

	if {[llength $best] == 1} { return [lindex $best 0] }

	return [lindex [lsort -integer -increasing $best] 0]
    }

    proc CutAt {location} {
	upvar 1 cross cross depc depc

	# It was decided to split the changeset at the given
	# location. This cuts a number of dependencies. Here we update
	# the cross information so that the break finder has accurate
	# data when we look at the generated fragments.

	set six [log visible? 6]

	# Note: The loop below could be made faster by keeping a map
	# from positions to the dependencies crossing. An extension of
	# CROSS, i.e. list of dependencies, counter is implied. Takes
	# a lot more memory however, and takes time to update here
	# (The inner loop is not incr -1, but ldelete).

	foreach dep [array names depc] {
	    set range $depc($dep)
	    # Check all dependencies still known, take their range and
	    # see if the break location falls within.

	    Border $range s e
	    if {$location < $s} continue ; # break before range, ignore
	    if {$location > $e} continue ; # break after range, ignore.

	    # This dependency crosses the break location. We remove it
	    # from the crossings counters, and then also from the set
	    # of known dependencies, as we are done with it.

	    Border $depc($dep) ds de
	    for {set loc $ds} {$loc <= $de} {incr loc} {
		incr cross($loc) -1
	    }
	    unset depc($dep)

	    if {!$six} continue

	    struct::list assign $dep parent child
	    log write 5 csets "Broke dependency [PD $parent] --> [PD $child]"
	}

	return
    }

    # Print identifying data for a revision (project, file, dotted rev
    # number), for high verbosity log output.
    # TODO: Replace with call to itemstr (list rev $id)

    proc PD {id} {
	foreach {p f r} [state run {
		SELECT P.name , F.name, R.rev
		FROM revision R, file F, project P
		WHERE R.rid = $id    -- Find specified file revision
		AND   F.fid = R.fid  -- Get file of the revision
		AND   P.pid = F.pid  -- Get project of the file.
	}] break
	return "'$p : $f/$r'"
    }

    # Printing one or more ranges, formatted, and only their border to
    # keep the strings short.

    proc PRs {ranges} {
	return [struct::list map $ranges [myproc PR]]
    }

    proc PR {range} {
	Border $range s e
	return <${s}...${e}>
    }

    proc Border {range sv ev} {
	upvar 1 $sv s $ev e
	set s [lindex $range 0]
	set e [lindex $range end]
	return
    }

    # # ## ### ##### ######## #############

    proc UnmapItems {thetype theitems} {
	# (*) We clear out the associated part of the myitemmap
	# in-memory index in preparation for new data, or as part of
	# object destruction. A simple unset is enough, we have no
	# symbol changesets at this time, and thus never more than one
	# reference in the list.

	upvar 1 myitemmap myitemmap self self
	foreach iid $theitems {
	    set key [list $thetype $iid]
	    unset myitemmap($key)
	    log write 8 csets {MAP- item <$key> $self = [$self str]}
	}
	return
    }

    proc MapItems {thetype theitems} {
	upvar 1 myitemmap myitemmap self self

	foreach iid $theitems {
	    set key [list $thetype $iid]
	    set myitemmap($key) $self
	    log write 8 csets {MAP+ item <$key> $self = [$self str]}
	}
	return
    }

    # # ## ### ##### ######## #############

    typevariable mychangesets         {} ; # List of all known
					   # changesets.

    # List of all known changesets of a type.
    typevariable mytchangesets -array {
	sym::branch {}
	sym::tag    {}
	rev         {}
    }

    typevariable myitemmap     -array {} ; # Map from items (tagged)
					   # to the list of changesets
					   # containing it. Each item
					   # can be used by only one
					   # changeset.
    typevariable myidmap   -array {} ; # Map from changeset id to
				       # changeset.

    typemethod all    {}    { return $mychangesets }
    typemethod of     {cid} { return $myidmap($cid) }
    typemethod ofitem {iid} { return $myitemmap($iid) }

    typemethod rev    {}    { return $mytchangesets(rev) }
    typemethod sym    {}    { return [concat \
					  ${mytchangesets(sym::branch)} \
					  ${mytchangesets(sym::tag)}] }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hastypeinfo    no  ; # no type introspection
    pragma -hasinfo        no  ; # no object introspection

    # # ## ### ##### ######## #############
}

##
## NOTE: The successor and predecessor methods defined by the classes
##       below are -- bottle necks --. Look for ways to make the SQL
##       faster.
##

# # ## ### ##### ######## ############# #####################
## Helper singleton. Commands for revision changesets.

snit::type ::vc::fossil::import::cvs::project::rev::rev {
    typemethod byrevision {} { return 1 }
    typemethod bysymbol   {} { return 0 }
    typemethod istag      {} { return 0 }
    typemethod isbranch   {} { return 0 }

    typemethod str {revision} {
	struct::list assign [state run {
	    SELECT R.rev, F.name, P.name
	    FROM   revision R, file F, project P
	    WHERE  R.rid = $revision -- Find specified file revision
	    AND    F.fid = R.fid     -- Get file of the revision
	    AND    P.pid = F.pid     -- Get project of the file.
	}] revnr fname pname
	return "$pname/${revnr}::$fname"
    }

    # result = list (mintime, maxtime)
    typemethod timerange {items} {
	set theset ('[join $items {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    SELECT MIN(R.date), MAX(R.date)
	    FROM revision R
	    WHERE R.rid IN $theset -- Restrict to revisions of interest
	}]]
    }

    # var(dv) = dict (revision -> list (revision))
    typemethod internalsuccessors {dv revisions} {
	upvar 1 $dv dependencies
	set theset ('[join $revisions {','}]')

	log write 14 csets internalsuccessors

	# See 'successors' below for the main explanation of
	# the various cases. This piece is special in that it
	# restricts the successors we look for to the same set of
	# revisions we start from. Sensible as we are looking for
	# changeset internal dependencies.

	array set dep {}

	state foreachrow [subst -nocommands -nobackslashes {
    -- (1) Primary child
	    SELECT R.rid AS xrid, R.child AS xchild
	    FROM   revision R
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.child IS NOT NULL    -- Has primary child
	    AND    R.child IN $theset     -- Which is also of interest
    UNION
    -- (2) Secondary (branch) children
	    SELECT R.rid AS xrid, B.brid AS xchild
	    FROM   revision R, revisionbranchchildren B
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.rid = B.rid          -- Select subset of branch children
	    AND    B.brid IN $theset      -- Which is also of interest
    UNION
    -- (4) Child of trunk root successor of last NTDB on trunk.
	    SELECT R.rid AS xrid, RA.child AS xchild
	    FROM revision R, revision RA
	    WHERE R.rid   IN $theset      -- Restrict to revisions of interest
	    AND   R.isdefault             -- Restrict to NTDB
	    AND   R.dbchild IS NOT NULL   -- and last NTDB belonging to trunk
	    AND   RA.rid = R.dbchild      -- Go directly to trunk root
	    AND   RA.child IS NOT NULL    -- Has primary child.
            AND   RA.child IN $theset     -- Which is also of interest
	}] {
	    # Consider moving this to the integrity module.
	    integrity assert {$xrid != $xchild} {Revision $xrid depends on itself.}
	    lappend dependencies($xrid) $xchild
	    set dep($xrid,$xchild) .
	}

	# The sql statements above looks only for direct dependencies
	# between revision in the changeset. However due to the
	# vagaries of meta data it is possible for two revisions of
	# the same file to end up in the same changeset, without a
	# direct dependency between them. However we know that there
	# has to be a an indirect dependency, be it through primary
	# children, branch children, or a combination thereof.

	# We now fill in these pseudo-dependencies, if no such
	# dependency exists already. The direction of the dependency
	# is actually irrelevant for this.

	# NOTE: This is different from cvs2svn. Our spiritual ancestor
	# does not use such pseudo-dependencies, however it uses a
	# COMMIT_THRESHOLD, a time interval commits should fall. This
	# will greatly reduces the risk of getting far separated
	# revisions of the same file into one changeset.

	# We allow revisions to be far apart in time in the same
	# changeset, but in turn need the pseudo-dependencies to
	# handle this.

	log write 14 csets {internal  [array size dep]}
	log write 14 csets {collected [array size dependencies]}
	log write 14 csets pseudo-internalsuccessors

	array set fids {}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT R.rid AS xrid, R.fid AS xfid
            FROM   revision R
            WHERE  R.rid IN $theset
	}] { lappend fids($xfid) $xrid }

	set groups {}
	foreach {fid rids} [array get fids] {
	    if {[llength $rids] < 2} continue
	    foreach a $rids {
		foreach b $rids {
		    if {$a == $b} continue
		    if {[info exists dep($a,$b)]} continue
		    if {[info exists dep($b,$a)]} continue
		    lappend dependencies($a) $b
		    set dep($a,$b) .
		    set dep($b,$a) .
		}
	    }
	    set n [llength $rids]
	    lappend groups [list $n [expr {($n*$n-$n)/2}]]
	}

	log write 14 csets {pseudo    [array size fids] ([lsort -index 0 -decreasing -integer $groups])}
	log write 14 csets {internal  [array size dep]}
	log write 14 csets {collected [array size dependencies]}
	log write 14 csets complete
	return
    }

    # result = 4-list (itemtype itemid nextitemtype nextitemid ...)
    typemethod loops {revisions} {
	# Note: Tags and branches cannot cause the loop. Their id's,
	# being of a fundamentally different type than the revisions
	# coming in cannot be in the set.

	set theset ('[join $revisions {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    -- (1) Primary child
	    SELECT R.rid, R.child
	    FROM   revision R
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.child IS NOT NULL    -- Has primary child
	    AND    R.child IN $theset     -- Loop
	    --
	    UNION
	    -- (2) Secondary (branch) children
	    SELECT R.rid, B.brid
	    FROM   revision R, revisionbranchchildren B
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.rid = B.rid          -- Select subset of branch children
	    AND    B.rid   IN $theset     -- Loop
	    --
	    UNION
	    -- (4) Child of trunk root successor of last NTDB on trunk.
	    SELECT R.rid, RA.child
	    FROM   revision R, revision RA
	    WHERE  R.rid    IN $theset     -- Restrict to revisions of interest
	    AND    R.isdefault             -- Restrict to NTDB
	    AND    R.dbchild IS NOT NULL   -- and last NTDB belonging to trunk
	    AND    RA.rid = R.dbchild      -- Go directly to trunk root
	    AND    RA.child IS NOT NULL    -- Has primary child.
	    AND    RA.child IN $theset     -- Loop
	}]]
    }

    # var(dv) = dict (item -> list (item)), item  = list (type id)
    typemethod successors {dv revisions} {
	upvar 1 $dv dependencies
	set theset ('[join $revisions {','}]')

	# The following cases specify when a revision S is a successor
	# of a revision R. Each of the cases translates into one of
	# the branches of the SQL UNION coming below.
	#
	# (1) S can be a primary child of R, i.e. in the same LOD. R
	#     references S directly. R.child = S(.rid), if it exists.
	#
	# (2) S can be a secondary, i.e. branch, child of R. Here the
	#     link is made through the helper table
	#     REVISIONBRANCHCHILDREN. R.rid -> RBC.rid, RBC.brid =
	#     S(.rid)
	#
	# (3) Originally this use case defined the root of a detached
	#     NTDB as the successor of the trunk root. This leads to a
	#     bad tangle later on. With a detached NTDB the original
	#     trunk root revision was removed as irrelevant, allowing
	#     the nominal root to be later in time than the NTDB
	#     root. Now setting this dependency will be backward in
	#     time. REMOVED.
	#
	# (4) If R is the last of the NTDB revisions which belong to
	#     the trunk, then the primary child of the trunk root (the
	#     '1.2' revision) is a successor, if it exists.

	# Note that the branches spawned from the revisions, and the
	# tags associated with them are successors as well.

	state foreachrow [subst -nocommands -nobackslashes {
    -- (1) Primary child
	    SELECT R.rid AS xrid, R.child AS xchild
	    FROM   revision R
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.child IS NOT NULL    -- Has primary child
    UNION
    -- (2) Secondary (branch) children
	    SELECT R.rid AS xrid, B.brid AS xchild
	    FROM   revision R, revisionbranchchildren B
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.rid = B.rid          -- Select subset of branch children
    UNION
    -- (4) Child of trunk root successor of last NTDB on trunk.
	    SELECT R.rid AS xrid, RA.child AS xchild
	    FROM revision R, revision RA
	    WHERE R.rid   IN $theset      -- Restrict to revisions of interest
	    AND   R.isdefault             -- Restrict to NTDB
	    AND   R.dbchild IS NOT NULL   -- and last NTDB belonging to trunk
	    AND   RA.rid = R.dbchild      -- Go directly to trunk root
	    AND   RA.child IS NOT NULL    -- Has primary child.
	}] {
	    # Consider moving this to the integrity module.
	    integrity assert {$xrid != $xchild} {Revision $xrid depends on itself.}
	    lappend dependencies([list rev $xrid]) [list rev $xchild]
	}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT R.rid AS xrid, T.tid AS xchild
	    FROM   revision R, tag T
	    WHERE  R.rid IN $theset       -- Restrict to revisions of interest
	    AND    T.rev = R.rid          -- Select tags attached to them
	}] {
	    lappend dependencies([list rev $xrid]) [list sym::tag $xchild]
	}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT R.rid AS xrid, B.bid AS xchild
	    FROM   revision R, branch B
	    WHERE  R.rid IN $theset       -- Restrict to revisions of interest
	    AND    B.root = R.rid         -- Select branches attached to them
	}] {
	    lappend dependencies([list rev $xrid]) [list sym::branch $xchild]
	}
	return
    }

    # result = list (changeset-id)
    typemethod cs_successors {revisions} {
        # This is a variant of 'successors' which maps the low-level
        # data directly to the associated changesets. I.e. instead
        # millions of dependency pairs (in extreme cases (Example: Tcl
        # CVS)) we return a very short and much more manageable list
        # of changesets.

	set theset ('[join $revisions {','}]')
	return [state run [subst -nocommands -nobackslashes {
    -- (1) Primary child
	    SELECT C.cid
	    FROM   revision R, csitem CI, changeset C
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.child IS NOT NULL    -- Has primary child
            AND    CI.iid = R.child       -- Select all changesets
            AND    C.cid = CI.cid         -- containing the primary child
            AND    C.type = 0             -- which are revision changesets
    UNION
    -- (2) Secondary (branch) children
	    SELECT C.cid
	    FROM   revision R, revisionbranchchildren B, csitem CI, changeset C
	    WHERE  R.rid   IN $theset     -- Restrict to revisions of interest
	    AND    R.rid = B.rid          -- Select subset of branch children
            AND    CI.iid = B.brid        -- Select all changesets
            AND    C.cid = CI.cid	  -- containing the branch
            AND    C.type = 0		  -- which are revision changesets
    UNION
    -- (4) Child of trunk root successor of last NTDB on trunk.
	    SELECT C.cid
	    FROM   revision R, revision RA, csitem CI, changeset C
	    WHERE  R.rid   IN $theset      -- Restrict to revisions of interest
	    AND    R.isdefault             -- Restrict to NTDB
	    AND    R.dbchild IS NOT NULL   -- and last NTDB belonging to trunk
	    AND    RA.rid = R.dbchild      -- Go directly to trunk root
	    AND    RA.child IS NOT NULL    -- Has primary child.
            AND    CI.iid = RA.child       -- Select all changesets
            AND    C.cid = CI.cid	   -- containing the primary child
            AND    C.type = 0		   -- which are revision changesets
    UNION
	    SELECT C.cid
	    FROM   revision R, tag T, csitem CI, changeset C
	    WHERE  R.rid in $theset        -- Restrict to revisions of interest
	    AND    T.rev = R.rid	   -- Select tags attached to them
            AND    CI.iid = T.tid          -- Select all changesets
            AND    C.cid = CI.cid	   -- containing the tags
            AND    C.type = 1		   -- which are tag changesets
    UNION
	    SELECT C.cid
	    FROM   revision R, branch B, csitem CI, changeset C
	    WHERE  R.rid in $theset        -- Restrict to revisions of interest
	    AND    B.root = R.rid	   -- Select branches attached to them
            AND    CI.iid = B.bid          -- Select all changesets
            AND    C.cid = CI.cid	   -- containing the branches
            AND    C.type = 2		   -- which are branch changesets
	}]]

	# Regarding rev -> branch|tag, we could consider looking at
	# the symbol of the branch|tag, its lod-symbol, and the
	# revisions on that lod, but don't. Because it is not exact
	# enough, the branch|tag would depend on revisions coming
	# after its creation on the parental lod.
    }

    # result = symbol name
    typemethod cs_lod {metaid revisions} {
	# Determines the name of the symbol which is the line of
	# development for the revisions in a changeset. The
	# information in the meta data referenced by the source metaid
	# is out of date by the time we come here (since pass
	# FilterSymbols), so it cannot be used. See the method 'run'
	# in file "c2f_pfiltersym.tcl" for more commentary on this.

	set theset ('[join $revisions {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    SELECT
	    DISTINCT L.name
	    FROM   revision R, symbol L
	    WHERE  R.rid in $theset        -- Restrict to revisions of interest
	    AND    L.sid = R.lod           -- Get lod symbol of revision
	}]]
    }
}

# # ## ### ##### ######## ############# #####################
## Helper singleton. Commands for tag symbol changesets.

snit::type ::vc::fossil::import::cvs::project::rev::sym::tag {
    typemethod byrevision {} { return 0 }
    typemethod bysymbol   {} { return 1 }
    typemethod istag      {} { return 1 }
    typemethod isbranch   {} { return 0 }

    typemethod str {tag} {
	struct::list assign [state run {
	    SELECT S.name, F.name, P.name
	    FROM   tag T, symbol S, file F, project P
	    WHERE  T.tid = $tag   -- Find specified tag
	    AND    F.fid = T.fid  -- Get file of tag
	    AND    P.pid = F.pid  -- Get project of file
	    AND    S.sid = T.sid  -- Get symbol of tag
	}] sname fname pname
	return "$pname/T'${sname}'::$fname"
    }

    # result = list (mintime, maxtime)
    typemethod timerange {tags} {
	# The range is defined as the range of the revisions the tags
	# are attached to.

	set theset ('[join $tags {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    SELECT MIN(R.date), MAX(R.date)
	    FROM   tag T, revision R
	    WHERE  T.tid IN $theset  -- Restrict to tags of interest
            AND    R.rid = T.rev     -- Select tag parent revisions
	}]]
    }

    # var(dv) = dict (item -> list (item)), item  = list (type id)
    typemethod successors {dv tags} {
	# Tags have no successors.
	return
    }

    # result = 4-list (itemtype itemid nextitemtype nextitemid ...)
    typemethod loops {tags} {
	# Tags have no successors, therefore cannot cause loops
	return {}
    }

    # result = list (changeset-id)
    typemethod cs_successors {tags} {
	# Tags have no successors.
	return
    }

    # result = symbol name
    typemethod cs_lod {sid tags} {
	# Determines the name of the symbol which is the line of
	# development for the tags in a changeset. Comes directly from
	# the symbol which is the changeset's source and its prefered
	# parent.

        return [state run {
 	    SELECT P.name
	    FROM preferedparent SP, symbol P
	    WHERE SP.sid = $sid
	    AND   P.sid = SP.pid
	}]
    }
}

# # ## ### ##### ######## ############# #####################
## Helper singleton. Commands for branch symbol changesets.

snit::type ::vc::fossil::import::cvs::project::rev::sym::branch {
    typemethod byrevision {} { return 0 }
    typemethod bysymbol   {} { return 1 }
    typemethod istag      {} { return 0 }
    typemethod isbranch   {} { return 1 }

    typemethod str {branch} {
	struct::list assign [state run {
	    SELECT S.name, F.name, P.name
	    FROM   branch B, symbol S, file F, project P
	    WHERE  B.bid = $branch  -- Find specified branch
	    AND    F.fid = B.fid    -- Get file of branch
	    AND    P.pid = F.pid    -- Get project of file
	    AND    S.sid = B.sid    -- Get symbol of branch
	}] sname fname pname
	return "$pname/B'${sname}'::$fname"
    }

    # result = list (mintime, maxtime)
    typemethod timerange {branches} {
	# The range of a branch is defined as the range of the
	# revisions the branches are spawned by. NOTE however that the
	# branches associated with a detached NTDB will have no root
	# spawning them, hence they have no real timerange any
	# longer. By using 0 we put them in front of everything else,
	# as they logically are.

	set theset ('[join $branches {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    SELECT IFNULL(MIN(R.date),0), IFNULL(MAX(R.date),0)
	    FROM  branch B, revision R
	    WHERE B.bid IN $theset   -- Restrict to branches of interest
            AND   R.rid = B.root     -- Select branch parent revisions
	}]]
    }

    # result = 4-list (itemtype itemid nextitemtype nextitemid ...)
    typemethod loops {branches} {
	# Note: Revisions and tags cannot cause the loop. Being of a
	# fundamentally different type they cannot be in the incoming
	# set of ids.

	set theset ('[join $branches {','}]')
	return [state run [subst -nocommands -nobackslashes {
	    SELECT B.bid, BX.bid
	    FROM   branch B, preferedparent P, branch BX
	    WHERE  B.bid IN $theset   -- Restrict to branches of interest
	    AND    B.sid = P.pid      -- Get the prefered branches via
	    AND    BX.sid = P.sid     -- the branch symbols
	    AND    BX.bid IN $theset  -- Loop
	}]]
    }

    # var(dv) = dict (item -> list (item)), item  = list (type id)
    typemethod successors {dv branches} {
	upvar 1 $dv dependencies
	# The first revision committed on a branch, and all branches
	# and tags which have it as their prefered parent are the
	# successors of a branch.

	set theset ('[join $branches {','}]')
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT B.bid AS xbid, R.rid AS xchild
	    FROM   branch B, revision R
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.first = R.rid      -- Get first revision on the branch
	}] {
	    lappend dependencies([list sym::branch $xbid]) [list rev $xchild]
	}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT B.bid AS xbid, BX.bid AS xchild
	    FROM   branch B, preferedparent P, branch BX
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.sid = P.pid        -- Get subordinate branches via the
	    AND    BX.sid = P.sid       -- prefered parents of their symbols
	}] {
	    lappend dependencies([list sym::branch $xbid]) [list sym::branch $xchild]
	}
	state foreachrow [subst -nocommands -nobackslashes {
	    SELECT B.bid AS xbid, T.tid AS xchild
	    FROM   branch B, preferedparent P, tag T
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.sid = P.pid        -- Get subordinate tags via the
	    AND    T.sid = P.sid        -- prefered parents of their symbols
	}] {
	    lappend dependencies([list sym::branch $xbid]) [list sym::tag $xchild]
	}
	return
    }

    # result = list (changeset-id)
    typemethod cs_successors {branches} {
        # This is a variant of 'successors' which maps the low-level
        # data directly to the associated changesets. I.e. instead
        # millions of dependency pairs (in extreme cases (Example: Tcl
        # CVS)) we return a very short and much more manageable list
        # of changesets.

	set theset ('[join $branches {','}]')
        return [state run [subst -nocommands -nobackslashes {
	    SELECT C.cid
	    FROM   branch B, revision R, csitem CI, changeset C
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.first = R.rid	-- Get first revision on the branch
            AND    CI.iid = R.rid       -- Select all changesets
            AND    C.cid = CI.cid	-- containing this revision
            AND    C.type = 0		-- which are revision changesets
    UNION
	    SELECT C.cid
	    FROM   branch B, preferedparent P, branch BX, csitem CI, changeset C
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.sid = P.pid	-- Get subordinate branches via the
	    AND    BX.sid = P.sid	-- prefered parents of their symbols
            AND    CI.iid = BX.bid      -- Select all changesets
            AND    C.cid = CI.cid	-- containing the subordinate branches
            AND    C.type = 2		-- which are branch changesets
    UNION
	    SELECT C.cid
	    FROM   branch B, preferedparent P, tag T, csitem CI, changeset C
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.sid = P.pid	-- Get subordinate tags via the
	    AND    T.sid = P.sid	-- prefered parents of their symbols
            AND    CI.iid = T.tid       -- Select all changesets
            AND    C.cid = CI.cid	-- containing the subordinate tags
            AND    C.type = 1		-- which are tag changesets
	}]]
	return
    }

    # result = symbol name
    typemethod cs_lod {sid branches} {
	# Determines the name of the symbol which is the line of
	# development for the branches in a changeset. Comes directly
	# from the symbol which is the changeset's source and its
	# prefered parent.

        return [state run {
 	    SELECT P.name
	    FROM preferedparent SP, symbol P
	    WHERE SP.sid = $sid
	    AND   P.sid = SP.pid
	}]
    }

    typemethod limits {branches} {
	# Notes. This method exists only for branches. It is needed to
	# get detailed information about a backward branch. It does
	# not apply to tags, nor revisions. The queries can also
	# restrict themselves to the revision sucessors/predecessors
	# of branches, as only they have ordering data and thus can
	# cause the backwardness.

	set theset ('[join $branches {','}]')

	set maxp [state run [subst -nocommands -nobackslashes {
	    -- maximal predecessor position per branch
	    SELECT B.bid, MAX (CO.pos)
	    FROM   branch B, revision R, csitem CI, changeset C, csorder CO
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.root = R.rid       -- Get branch root revisions
	    AND    CI.iid = R.rid       -- Get changesets containing the
	    AND    C.cid = CI.cid       -- root revisions, which are
	    AND    C.type = 0           -- revision changesets
	    AND    CO.cid = C.cid       -- Get their topological ordering
	    GROUP BY B.bid
	}]]

	set mins [state run [subst -nocommands -nobackslashes {
	    -- minimal successor position per branch
	    SELECT B.bid, MIN (CO.pos)
	    FROM   branch B, revision R, csitem CI, changeset C, csorder CO
	    WHERE  B.bid IN $theset     -- Restrict to branches of interest
	    AND    B.first = R.rid      -- Get the first revisions on the branches
	    AND    CI.iid = R.rid       -- Get changesets containing the
	    AND    C.cid = CI.cid	-- first revisions, which are
	    AND    C.type = 0		-- revision changesets
	    AND    CO.cid = C.cid	-- Get their topological ordering
	    GROUP BY B.bid
	}]]

        return [list $maxp $mins]
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal
}

# # ## ### ##### ######## ############# #####################
##

namespace eval ::vc::fossil::import::cvs::project {
    namespace export rev
    namespace eval rev {
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::fossil::import::cvs::integrity
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register csets

	# Set up the helper singletons
	namespace eval rev {
	    namespace import ::vc::fossil::import::cvs::state
	    namespace import ::vc::fossil::import::cvs::integrity
	    namespace import ::vc::tools::log
	}
	namespace eval sym::tag {
	    namespace import ::vc::fossil::import::cvs::state
	    namespace import ::vc::fossil::import::cvs::integrity
	    namespace import ::vc::tools::log
	}
	namespace eval sym::branch {
	    namespace import ::vc::fossil::import::cvs::state
	    namespace import ::vc::fossil::import::cvs::integrity
	    namespace import ::vc::tools::log
	}
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

package provide vc::fossil::import::cvs::project::rev 1.0
return
