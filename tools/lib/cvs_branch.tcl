

namespace eval ::vc::cvs::ws::branch {}

# Trivial storage of all branch data as a rectangular table.  We can
# think up a better suited storage system later, when we know what
# type of queries are made to this module.

proc ::vc::cvs::ws::branch::def {f dv deflist} {
    upvar 1 $dv date
    variable bra
    foreach {tag rev} $deflist {
	# ignore non-branch tags
	if {[llength [split $rev .]] < 4} continue

	if 0 {
	    if { ($rev ne "1.1.1.1") && ![string match *.0.2 $rev] } {
		# 1.1.1.1 is the base of vendor branches, usually. *.0.y
		# is the base of regular branches where nothing is on the
		# branch yet, only its root is marked. Everything else is
		# noteworthy for now.
		puts $f/$rev/$tag
	    }
	}

	set root [revroot $rev]
	lappend bra [list $date($root) $tag $f $rev]
    }
}

proc ::vc::cvs::ws::branch::revroot {rev} {
    return [join [lrange [split $rev .] 0 end-2] .]
}


    # ! Files in a branch can appear only after their root revision
    #   exists. This can be checked against the time of the cset which
    #   is our base. Branches which have no files yet can be eliminated
    #   from consideration.

    # ! All files noted by the base cset as added/modified have to be
    #   in the branch root. Branches which do not have such a file can
    #   be eliminated from consideration.

    # ! The versions of the added/modified files in the base have
    #   match the versions in the branch root. In the sense that they
    #   have to be equal or sucessors. The later implies identity in the
    #   upper parts (only the last 2 parts are relevant), and equal
    #   length.

    # This gives us the branch, and, due to the time information a
    # signature for the root.

    #? Can search for the root based on this signature fail ?
    #  Yes. Because the signature may contain files which were not
    #  actually yet in the root, despite being able to. And which were
    #  not modified by the base, so the check 2 above still passes.

    # -> Search for the full signature first, then drop the youngest
    # files, search again until match. Check the result against the
    # base, that all needed files are present.

    # However - Can search for the root based on the cset data (needed
    # files). Gives us another set of candidate roots. Intersect!


proc ::vc::cvs::ws::branch::find {csvalue} {
    array set cs $csvalue

    #variable bra
    #puts ___________________________________________
    #puts [join [lsort -index 0 [lsort -index 1 $bra]] \n]

    Signatures     bd [TimeRelevant $cs(date)]
    DropIncomplete bd [concat $cs(added) $cs(changed)]

    #puts ___________________________________________
    #parray bd

    if {[array size bd] < 1} {
	puts "NO BRANCH"
	# Deal how?
	# - Abort
	# - Ignore this changeset and try the next one
	#   (Which has higher probability of not matching as it might
	#    be the successor in the branch to this cset and not a base).
	puts ""
	parray cs
	exit
    } elseif {[array size bd] > 1} {

	# While we might have found several tag they may all refer to
	# the same set of files. If that is so we consider them
	# identical and take one as representative of all.

	set su {}
	foreach {t s} [array get bd] {
	    lappend su [DictSort $s]
	}
	if {[llength [lsort -unique $su]] > 1} {
	    puts "AMBIGOUS. The following branches match:"
	    # Deal how? S.a.
	    puts \t[join [array names bd] \n\t]
	    puts ""
	    parray cs
	    exit
	}
	# Fall through ...
    }

    set tg [lindex [array names bd] 0]
    set rs [RootOf $bd($tg)]

    #puts "BRANCH = $tg"
    #puts "ROOTSG = $rs"

    return [list $tg $rs]
}


proc ::vc::cvs::ws::branch::has {ts needed} {
    #variable bra
    #puts ___________________________________________
    #puts [join [lsort -index 0 [lsort -index 1 $bra]] \n]

    Signatures     bd [TimeRelevant $ts]
    DropIncomplete bd $needed

    #puts ___________________________________________
    #parray bd

    if {[array size bd] < 1} {
	puts "NO BRANCH"
	# Deal how?
	# - Abort
	# - Ignore this changeset and try the next one
	#   (Which has higher probability of not matching as it might
	#    be the successor in the branch to this cset and not a base).
	exit
    } elseif {[array size bd] > 1} {
	puts "AMBIGOUS. Following branches match:"
	# Deal how? S.a.
	puts \t[join [array names bd] \n\t]
	exit
    }

    set tg [lindex [array names bd] 0]

    #puts "BRANCH = $tg"

    return $tg
}



proc ::vc::cvs::ws::branch::RootOf {dict} {
    set res {}
    foreach {f r} $dict {
	lappend res $f [revroot $r]
    }
    return $res
}

proc ::vc::cvs::ws::branch::DictSort {dict} {
    array set a $dict
    set r {}
    foreach k [lsort [array names a]] {
	lappend r $k $a($k)
    }
    return $r
}

proc ::vc::cvs::ws::branch::DropIncomplete {bv needed} {
    upvar 1 $bv bdata

    # Check the needed files against the branch signature. If files
    # are missing or not of a matching version drop the branch from
    # further consideration.

    foreach {tag sig} [array get bdata] {
	array set rev $sig
	foreach {file rv} $needed {
	    if {![info exists rev($file)] || ![successor $rv $rev($file)]} {
		# file in cset is not in the branch or is present, but
		# not proper version (different lengths, not matching
		# in upper 0..end-2 parts, not equal|successor).
		unset bdata($tag)
		break
	    } 
	    continue
	}
	unset rev
    }
    return
}

proc ::vc::cvs::ws::branch::successor {ra rb} {
    # a successor-of b ?

    set la [split $ra .]
    set lb [split $rb .]
    if {
	([llength $la]         != [llength $lb])         ||
	([lrange  $la 0 end-2] ne [lrange  $lb 0 end-2]) ||
	([package vcompare $ra $rb] < 0)
    } {
	return 0
    } else {
	return 1
    }
}

proc ::vc::cvs::ws::branch::rootSuccessor {ra rb} {
    # a root-successor-of b ? (<=> b root version of a ?)

    if {$rb eq [revroot $ra]} {
	return 1
    } else {
	return 0
    }
}

proc ::vc::cvs::ws::branch::Signatures {bv deflist} {
    upvar 1 $bv bdata
    # Sort branch data by symbolic name for the upcoming checks, and
    # generate file revision signatures.

    array set bdata {}
    foreach item $deflist {
	# item = timestamp tag file revision
	foreach {__ tag file rev} $item break
	lappend bdata($tag) $file $rev
    }

    #puts ___________________________________________
    #parray bdata

    return
}

proc ::vc::cvs::ws::branch::TimeRelevant {date} {
    variable bra

    # Retrieve the branch data which definitely comes before (in time)
    # the candidate cset. Only this set is relevant to further checks
    # and filters.

    set res {}
    foreach item $bra {
	# item = timestamp tag file revision
	#        0         1   2    3
	if {[package vcompare [lindex $item 0] $date] > 0} continue
	lappend res $item
    }

    #puts ___________________________________________
    #puts [join [lsort -index 0 [lsort -index 1 $res]] \n]
    return $res
}


namespace eval ::vc::cvs::ws::branch {
    variable bra {}

    namespace export def find successor rootSuccessor revroot has
}

package provide vc::cvs::ws::branch 1.0
return




    # Queries ... 
    # - Get set of files and revs for branch B which can be in it by the time T
    # - Check if a file referenced a/m instruction is in a set of files
    #   and revision, identical or proper sucessor.
    # => Combination
    #    Can branch B match the cset file a/m at time T ?
    # => Full combination
    #    Give me the list of branches which can match the cset file a/m
    #    at time T.

    # Branch DB organization => (Tag -> (Time -> (File -> Rev)))
    # The full combination actually does not need a complex structure.
    # We can simply scan a plain list of branch data.
    # The only alternative is an inverted index.
    # Time -> ((File -> Rev) -> Tag). Difficult to process.
    # Linear scan:
    # - Time after T   => drop
    # - File !in a/m   => drop
    # - Version !match => drop
    # -- Collect tag
    # Then lsort -unique for our result.
    # NO - The file check is inverted - All files have to be in a/m for the base, not a/m in files
    # == - This also breaks the issue for same-branch detection -
    #    future csets in the branch do not have that property.

    puts ___________________________________________
    # Show only branch data which definitely comes before the
    # candidate cset

    array set n [concat $cs(added) $cs(changed)]
    set xx {}
    set bb {}
    ::foreach x $bra {
	::foreach {ts tag f r} $x break
	if {[package vcompare $ts $cs(date)] > 0} continue
	if {![info exists n($f)]} continue
	if {
	    ([llength [split $n($f) .]] != [llength [split $r .]]) ||
	    ([lrange [split $n($f) .] 0 end-2] ne [lrange [split $r .] 0 end-2]) ||
	    ([package vcompare $n($f) $r] < 0)
	} continue
	lappend xx $x
	lappend bb $tag
    }
    puts [join [lsort -index 0 [lsort -index 1 $xx]] \n]
    puts [join [lsort -unique $bb] \n]

exit


