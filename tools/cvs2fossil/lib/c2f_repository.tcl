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

## Repository manager. Keeps projects and their files around.

package provide vc::fossil::import::cvs::repository 1.0

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                          ; # Required runtime.
package require snit                             ; # OO system.
package require vc::tools::trouble               ; # Error reporting.
package require vc::tools::log                   ; # User feedback.
package require vc::tools::misc                  ; # Text formatting.
package require vc::fossil::import::cvs::project ; # CVS projects.
package require vc::fossil::import::cvs::state   ; # State storage.
package require struct::list                     ; # List operations.
package require fileutil                         ; # File operations.

# # ## ### ##### ######## ############# #####################
## 

snit::type ::vc::fossil::import::cvs::repository {
    # # ## ### ##### ######## #############
    ## Public API

    typemethod base {path} {
	# Could be checked, easier to defer to the overall validation.
	set mybase $path
	return
    }

    typemethod add {path} {
	# Most things cannot be checked immediately, as the base is
	# not known while projects are added. We can and do check for
	# uniqueness. We accept multiple occurences of a name, and
	# treat them as a single project.

	if {[lsearch -exact $myprojpaths $path] >= 0} return
	lappend myprojpaths $path
	return
    }

    typemethod trunkonly! {} { set mytrunkonly 1 ; return }
    typemethod trunkonly  {} { return $mytrunkonly }

    typemethod projects {} {
	return [TheProjects]
    }

    typemethod base? {} { return $mybase }

    typemethod validate {} {
	if {![IsRepositoryBase $mybase msg]} {
	    trouble fatal $msg
	    # Without a good base directory checking any projects is
	    # wasted time, so we leave now.
	    return
	}
	foreach pp $myprojpaths {
	    if {![IsProjectBase $mybase/$pp $mybase/CVSROOT msg]} {
		trouble fatal $msg
	    }
	}
	return
    }

    typemethod defauthor {a} {
	if {![info exists myauthor($a)]} {
	    set myauthor($a) [incr myauthorcnt]
	    log write 7 repository "author '$a' =  $myauthor($a)"
	}
	return $myauthor($a)
    }

    typemethod defcmessage {cm} {
	if {![info exists mycmsg($cm)]} {
	    set mycmsg($cm) [set cid [incr mycmsgcnt]]
	    set mycmsginv($cid) $cm
	    log write 7 repository "cmessage '$cm' =  $cid"
	}
	return $mycmsg($cm)
    }

    typemethod defsymbol {pid name} {
	set key [list $pid $name]
	if {![info exists mysymbol($key)]} {
	    set mysymbol($key) [incr mysymbolcnt]
	    log write 7 repository "symbol ($key) =  $mysymbol($key)"
	}
	return $mysymbol($key)
    }

    typemethod defmeta {pid bid aid cid} {
	set key [list $pid $bid $aid $cid]
	if {![info exists mymeta($key)]} {
	    set mymeta($key) [set mid [incr mymetacnt]]
	    set mymetainv($mid) $key
	    log write 7 repository "meta ($key) =  $mymeta($key)"
	}
	return $mymeta($key)
    }

    typemethod commitmessageof {metaid} {
	struct::list assign $mymetainv($metaid) pid bid aid cid
	return $mycmsginv($cid)
    }

    # pass I results
    typemethod printstatistics {} {
	set prlist [TheProjects]
	set npr [llength $prlist]

	log write 2 repository "Scanned [nsp $npr project]"

	if {$npr > 1} {
	    set  bmax [max [struct::list map $prlist [myproc .BaseLength]]]
	    incr bmax 2
	    set  bfmt %-${bmax}s

	    set  nmax [max [struct::list map $prlist [myproc .NFileLength]]]
	    set  nfmt %${nmax}s
	} else {
	    set bfmt %s
	    set nfmt %s
	}

	set keep {}
	foreach p $prlist {
	    set nfiles [llength [$p filenames]]
	    set line "Project [format $bfmt \"[$p printbase]\"] : [format $nfmt $nfiles] [sp $nfiles file]"
	    if {$nfiles < 1} {
		append line ", dropped"
	    } else {
		lappend keep $p
	    }
	    log write 2 repository $line
	}

	if {![llength $keep]} {
	    trouble warn "Dropped all projects"
	} elseif {$npr == [llength $keep]} {
	    log write 2 repository "Keeping all projects"
	} else {
	    log write 2 repository "Keeping [nsp [llength $keep] project]"
	    trouble warn "Dropped [nsp [expr {$npr - [llength $keep]}] {empty project}]"
	}

	# Keep reduced set of projects.
	set projects $keep
	return
    }

    # pass I persistence
    typemethod persist {} {
	state transaction {
	    foreach p [TheProjects] { $p persist }
	}
	return
    }

    typemethod load {} {
	array set pr {}
	state transaction {
	    foreach   {pid  name} [state run {
		SELECT pid, name FROM project ;
	    }] {
		lappend myprojpaths $name
		lappend myprojects [set pr($pid) [project %AUTO% $name $type]]
		$pr($pid) setid $pid
	    }
	    foreach   {fid  pid  name  visible  exec} [state run {
		SELECT fid, pid, name, visible, exec FROM file ;
	    }] {
		$pr($pid) addfile $name $visible $exec
	    }
	}
	return
    }

    # pass II results
    typemethod printrevstatistics {} {
	log write 2 repository "Scanned ..."
	# number of revisions, symbols, repository wide, per project ...
	return
    }

    # pass II persistence
    typemethod persistrev {} {
	state transaction {
	    SaveAuthors
	    SaveCommitMessages
	    # TODO: Save symbols of all projects (before the revisions
	    # in the projects, as they are referenced by the meta
	    # tuples)
	    SaveMeta
	    foreach p [TheProjects] { $p persistrev }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## State

    typevariable mybase           {} ; # Base path to CVS repository.
    typevariable myprojpaths      {} ; # List of paths to all declared
				       # projects, relative to mybase.
    typevariable myprojects       {} ; # List of objects for all
				       # declared projects.
    typevariable myauthor  -array {} ; # Names of all authors found,
				       # maps to their ids.
    typevariable myauthorcnt      0  ; # Counter for author ids.
    typevariable mycmsg    -array {} ; # All commit messages found,
				       # maps to their ids.
    typevariable mycmsginv -array {} ; # Inverted index, keyed by id.
    typevariable mycmsgcnt        0  ; # Counter for message ids.
    typevariable mymeta    -array {} ; # Maps all meta data tuples
				       # (project, branch, author,
				       # cmessage) to their ids.
    typevariable mymetainv -array {} ; # Inverted index, keyed by id.
    typevariable mymetacnt        0  ; # Counter for meta ids.
    typevariable mysymbol -array  {} ; # Map symbols identified by
				       # project and name to their
				       # id. This information is not
				       # saved directly.
    typevariable mysymbolcnt      0  ; # Counter for symbol ids.
    typevariable mytrunkonly      0  ; # Boolean flag. Set by option
				       # processing when the user
				       # requested a trunk-only import

    # # ## ### ##### ######## #############
    ## Internal methods

    proc .BaseLength {p} {
	return [string length [$p printbase]]
    }

    proc .NFileLength {p} {
	return [string length [llength [$p filenames]]]
    }

    proc IsRepositoryBase {path mv} {
	::variable mybase
	upvar 1 $mv msg
	if {![fileutil::test $mybase         edr msg {CVS Repository}]}      {return 0}
	if {![fileutil::test $mybase/CVSROOT edr msg {CVS Admin Directory}]} {return 0}
	return 1
    }

    proc IsProjectBase {path admin mv} {
	upvar 1 $mv msg
	if {![fileutil::test $path edr msg Project]} {return 0}
	if {
	    ($path eq $admin) ||
	    [string match $admin/* $path]
	} {
	    set msg "Administrative subdirectory $path cannot be a project"
	    return 0
	}
	return 1
    }

    proc TheProjects {} {
	upvar 1 type type
	::variable myprojects
	::variable myprojpaths

	if {![llength $myprojects]} {
	    set myprojects [EmptyProjects $myprojpaths]
	}
	return $myprojects
    }

    proc EmptyProjects {projpaths} {
	::variable mybase
	upvar 1 type type
	set res {}
	if {[llength $projpaths]} {
	    foreach pp $projpaths {
		lappend res [project %AUTO% $pp $type]
	    }
	} else {
	    # Base is the single project.
	    lappend res [project %AUTO% "" $type]
	}
	return $res
    }

    proc SaveAuthors {} {
	::variable myauthor
	foreach {name aid} [array get myauthor] {
	    state run {
		INSERT INTO author ( aid,  name)
		VALUES             ($aid, $name);
	    }
	}
	return
    }

    proc SaveCommitMessages {} {
	::variable mycmsg
	foreach {text cid} [array get mycmsg] {
	    state run {
		INSERT INTO cmessage ( cid,  text)
		VALUES               ($cid, $text);
	    }
	}
	return
    }

    proc SaveMeta {} {
	::variable mymeta
	foreach {key mid} [array get mymeta] {
	    struct::list assign $key pid bid aid cid
	    if {$bid eq ""} {
		# Trunk. Encoded as NULL.
		state run {
		    INSERT INTO meta ( mid,  pid,  bid,  aid,  cid)
		    VALUES           ($mid, $pid, NULL, $aid, $cid);
		}
	    } else {
		state run {
		    INSERT INTO meta ( mid,  pid,  bid,  aid,  cid)
		    VALUES           ($mid, $pid, $bid, $aid, $cid);
		}
	    }
	}
	return
    }

    # # ## ### ##### ######## #############
    ## Configuration

    pragma -hasinstances   no ; # singleton
    pragma -hastypeinfo    no ; # no introspection
    pragma -hastypedestroy no ; # immortal

    # # ## ### ##### ######## #############
}

namespace eval ::vc::fossil::import::cvs {
    namespace export repository
    namespace eval repository {
	namespace import ::vc::fossil::import::cvs::project
	namespace import ::vc::fossil::import::cvs::state
	namespace import ::vc::tools::misc::*
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register repository
    }
}

# # ## ### ##### ######## ############# #####################
## Ready

return
