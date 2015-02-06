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

## Repository manager. Keeps projects and their files around.

package provide vc::fossil::import::cvs::repository 1.0

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.4                          ; # Required runtime.
package require snit                             ; # OO system.
package require vc::tools::trouble               ; # Error reporting.
package require vc::tools::log                   ; # User feedback.
package require vc::tools::misc                  ; # Text formatting.
package require vc::tools::id                    ; # Indexing and id generation.
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

    typemethod defauthor   {a}               { $myauthor put $a }
    typemethod defcmessage {cm}              { $mycmsg   put $cm }
    typemethod defsymbol   {pid name}        { $mysymbol put [list $pid $name] }
    typemethod defmeta     {pid bid aid cid} { $mymeta   put [list $pid $bid $aid $cid] }

    typemethod commitmessageof {mid} {
	struct::list assign [$mymeta keyof $mid] pid bid aid cid
	return [$mycmsg keyof $cid]
    }

    typemethod getmeta {mid} {
	struct::list assign [$mymeta keyof $mid] pid bid aid cid
	return [list \
		    $myprojmap($pid) \
		    [$mysymbol keyof $bid] \
		    [$myauthor keyof $aid] \
		    [$mycmsg   keyof $cid]]
    }

    # pass I results
    typemethod printstatistics {} {
	set prlist [TheProjects]
	set npr [llength $prlist]

	log write 2 repository "Statistics: Scanned [nsp $npr project]"

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
	    set line "Statistics: Project [format $bfmt \"[$p printbase]\"] : [format $nfmt $nfiles] [sp $nfiles file]"
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
	::variable myprojmap
	state transaction {
	    foreach p [TheProjects] {
		$p persist
		set myprojmap([$p id]) $p
	    }
	}
	return
    }

    typemethod load {} {
	state transaction {
	    state foreachrow {
		SELECT pid, name FROM project ;
	    } {
		set project [project %AUTO% $name $type]

		lappend myprojpaths $name
		lappend myprojects  $project
		set myprojmap($pid) $project
		$project setid $pid
	    }
	    state foreachrow {
		SELECT fid, pid, name, visible, exec FROM file ;
	    } {
		$myprojmap($pid) addfile $name $visible $exec $fid
	    }
	}
	return
    }

    # pass II results
    typemethod printrevstatistics {} {
	log write 2 repository "Revision statistics"
	# number of revisions, symbols, repository wide, and per project ...

	set rcount [state one { SELECT COUNT (*) FROM revision }]
	set tcount [state one { SELECT COUNT (*) FROM tag      }]
	set bcount [state one { SELECT COUNT (*) FROM branch   }]
	set scount [state one { SELECT COUNT (*) FROM symbol   }]
	set acount [state one { SELECT COUNT (*) FROM author   }]
	set ccount [state one { SELECT COUNT (*) FROM cmessage }]
	set fmt %[string length [max [list $rcount $tcount $bcount $scount $acount $ccount]]]s

	log write 2 repository "Statistics: [format $fmt $rcount] [sp $rcount revision]"
	log write 2 repository "Statistics: [format $fmt $tcount] [sp $tcount tag]"
	log write 2 repository "Statistics: [format $fmt $bcount] [sp $bcount branch branches]"
	log write 2 repository "Statistics: [format $fmt $scount] [sp $scount symbol]"
	log write 2 repository "Statistics: [format $fmt $acount] [sp $acount author]"
	log write 2 repository "Statistics: [format $fmt $ccount] [sp $ccount {log message}]"

	set prlist [TheProjects]
	set npr [llength $prlist]

	if {$npr > 1} {
	    set  bmax [max [struct::list map $prlist [myproc .BaseLength]]]
	    incr bmax 2
	    set  bfmt %-${bmax}s
	} else {
	    set bfmt %s
	}

	foreach p $prlist {
	    set pid [$p id]
	    set prefix "Project [format $bfmt \"[$p printbase]\"]"
	    regsub -all {[^	]} $prefix { } blanks
	    set sep " : "

	    set rcount [state one { SELECT COUNT (*) FROM revision R, file F WHERE R.fid = F.fid AND F.pid = $pid }]
	    set tcount [state one { SELECT COUNT (*) FROM tag T,      file F WHERE T.fid = F.fid AND F.pid = $pid }]
	    set bcount [state one { SELECT COUNT (*) FROM branch B,   file F WHERE B.fid = F.fid AND F.pid = $pid }]
	    set scount [state one { SELECT COUNT (*) FROM symbol             WHERE pid = $pid                     }]
	    set acount [state one { SELECT COUNT (*) FROM author   WHERE aid IN (SELECT DISTINCT aid FROM meta WHERE pid = $pid) }]
	    set ccount [state one { SELECT COUNT (*) FROM cmessage WHERE cid IN (SELECT DISTINCT cid FROM meta WHERE pid = $pid) }]

	    log write 2 repository "Statistics: $prefix$sep[format $fmt $rcount] [sp $rcount revision]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $tcount] [sp $tcount tag]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $bcount] [sp $bcount branch branches]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $scount] [sp $scount symbol]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $acount] [sp $acount author]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $ccount] [sp $ccount {log message}]"
	}
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

    typemethod loadsymbols {} {
	state transaction {
	    # We load the symbol ids at large to have the mapping
	    # right from the beginning.

	    state foreachrow {
		SELECT sid, pid, name, tag_count AS tc, branch_count AS bc, commit_count AS cc
		FROM symbol
	    } {
		$mysymbol map $sid [list $pid $name]
		set project $myprojmap($pid)

		set force  [$project hassymbol $name]
		set symbol [$project getsymbol $name]

		# Forcing happens only for the trunks.
		if {$force} { $symbol forceid $sid }

		# Set the loaded counts.
		$symbol defcounts $tc $bc $cc

		# Note: The type is neither retrieved nor set, for
		# this is used to load the pass II data, which means
		# that everything is 'undefined' at this point anyway.

		# future: $symbol load (blockers, and parents)
	    }

	    # Beyond the symbols we also load the author, commit log,
	    # and meta information.

	    state foreachrow {
		SELECT aid, name AS aname FROM author
	    } {
		$myauthor map $aid $aname
	    }
	    state foreachrow {
		SELECT cid, text FROM cmessage
	    } {
		$mycmsg map $cid $text
	    }
	    state foreachrow {
		SELECT mid, pid, bid, aid, cid FROM meta
	    } {
		$mymeta map $mid [list $pid $bid $aid $cid]
	    }
	}
	return
    }

    typemethod determinesymboltypes {} {
	foreach project [TheProjects] {
	    $project determinesymboltypes
	}
	return
    }

    typemethod projectof {pid} {
	return $myprojmap($pid)
    }


    # pass IV+ results
    typemethod printcsetstatistics {} {
	log write 2 repository "Changeset statistics"
	# number of revisions, symbols, repository wide, and per project ...

	set ccount [state one { SELECT COUNT (*) FROM changeset                }]
	set rcount [state one { SELECT COUNT (*) FROM changeset WHERE type = 0 }]
	set tcount [state one { SELECT COUNT (*) FROM changeset WHERE type = 1 }]
	set bcount [state one { SELECT COUNT (*) FROM changeset WHERE type = 2 }]
	set fmt %[string length [max [list $ccount $rcount $tcount $bcount]]]s

	log write 2 repository "Statistics: [format $fmt $ccount] [sp $ccount changeset]"
	log write 2 repository "Statistics: [format $fmt $rcount] [sp $rcount {revision changeset}]"
	log write 2 repository "Statistics: [format $fmt $tcount] [sp $tcount {tag symbol changeset}]"
	log write 2 repository "Statistics: [format $fmt $bcount] [sp $bcount {branch symbol changeset}]"

	set prlist [TheProjects]
	set npr [llength $prlist]

	if {$npr > 1} {
	    set  bmax [max [struct::list map $prlist [myproc .BaseLength]]]
	    incr bmax 2
	    set  bfmt %-${bmax}s
	} else {
	    set bfmt %s
	}

	foreach p $prlist {
	    set pid [$p id]
	    set prefix "Project [format $bfmt \"[$p printbase]\"]"
	    regsub -all {[^	]} $prefix { } blanks
	    set sep " : "

	    set ccount [state one { SELECT COUNT (*) FROM changeset WHERE pid = $pid              }]
	    set rcount [state one { SELECT COUNT (*) FROM changeset WHERE pid = $pid AND type = 0 }]
	    set tcount [state one { SELECT COUNT (*) FROM changeset WHERE pid = $pid AND type = 1 }]
	    set bcount [state one { SELECT COUNT (*) FROM changeset WHERE pid = $pid AND type = 2 }]

	    log write 2 repository "Statistics: $prefix$sep[format $fmt $ccount] [sp $ccount changeset]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $rcount] [sp $rcount {revision changeset}]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $tcount] [sp $tcount {tag symbol changeset}]"
	    log write 2 repository "Statistics: $blanks$sep[format $fmt $bcount] [sp $bcount {branch symbol changeset}]"
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
    typevariable myprojmap -array {} ; # Map from project ids to their
				       # objects.
    typevariable myauthor         {} ; # Names of all authors found,
				       # maps to their ids.
    typevariable mycmsg           {} ; # All commit messages found,
				       # maps to their ids.
    typevariable mymeta           {} ; # Maps all meta data tuples
				       # (project, branch, author,
				       # cmessage) to their ids.
    typevariable mysymbol         {} ; # Map symbols identified by
				       # project and name to their
				       # id. This information is not
				       # saved directly.
    typevariable mytrunkonly      0  ; # Boolean flag. Set by option
				       # processing when the user
				       # requested a trunk-only import

    # # ## ### ##### ######## #############
    ## Internal methods

    typeconstructor {
	set myauthor [vc::tools::id %AUTO%]
	set mycmsg   [vc::tools::id %AUTO%]
	set mymeta   [vc::tools::id %AUTO%]
	set mysymbol [vc::tools::id %AUTO%]
	return
    }

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
	foreach {name aid} [$myauthor get] {
	    state run {
		INSERT INTO author ( aid,  name)
		VALUES             ($aid, $name);
	    }
	}
	return
    }

    proc SaveCommitMessages {} {
	::variable mycmsg
	foreach {text cid} [$mycmsg get] {
	    state run {
		INSERT INTO cmessage ( cid,  text)
		VALUES               ($cid, $text);
	    }
	}
	return
    }

    proc SaveMeta {} {
	::variable mymeta
	foreach {key mid} [$mymeta get] {
	    struct::list assign $key pid bid aid cid
	    state run {
		INSERT INTO meta ( mid,  pid,  bid,  aid,  cid)
		VALUES           ($mid, $pid, $bid, $aid, $cid);
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
	namespace import ::vc::tools::id
	namespace import ::vc::tools::trouble
	namespace import ::vc::tools::log
	log register repository
    }
}

# # ## ### ##### ######## ############# #####################
## Ready
return
