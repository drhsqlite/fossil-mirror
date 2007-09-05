#!/bin/sh
# -*- tcl -*- \
exec tclsh "$0" ${1+"$@"}

# -----------------------------------------------------------------------------

# Import the trunk of a CVS repository wholesale into a fossil repository.

# Limitations implicitly mentioned:
# - No incremental import.
# - No import of branches.

# WIBNI features (beyond eliminating the limitations):
# - Restrict import to specific directory subtrees (SF projects use
#   one repository for several independent modules. Examples: tcllib
#   -> tcllib, tklib, tclapps, etc.). The restriction would allow import
#   of only a specific module.
# - Related to the previous, strip elements from the base path to keep
#   it short.
# - Export to CVS, trunk, possibly branches. I.e. extend the system to be
#   a full bridge. Either Fossil or CVS could be the master repository.

# HACKS. I.e. I do not know if the 'fixes' I use are the correct way
#        of handling the encountered situations.
#
# - File F has archives F,v and Attic/F,v. Currently I will ignore the
#   file in the Attic.
#   Examples: sqlite/os_unix.h
#
# - A specific revision of a file F cannot be checked out (reported
#   error is 'invalid change text'). This indicates a corrupt RCS
#   file, one or more delta are bad. We report but ignore the problem
#   in a best-effort attempt at getting as much history as possible.
#   Examples: tcllib/tklib/modules/tkpiechart/pie.tcl

# -----------------------------------------------------------------------------
# Make private packages accessible.

lappend auto_path [file join [file dirname [info script]] lib]

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require cvs    ; # Frontend, reading from source repository
package require fossil ; # Backend,  writing to destination repository.

# -----------------------------------------------------------------------------

proc main {} {
    global argv tot nto cvs fossil ntrunk

    commandline

    fossil::feedback Write ; # Setup progress feedback from the libraries
    cvs::feedback    Write

    cvs::at       $cvs  ; # Define location of CVS repository
    cvs::scan           ; # Gather revision data from the archives
    cvs::csets          ; # Group changes into sets
    cvs::rtree          ; # Build revision tree (trunk only right now).

    set tot 0.0
    set nto 0

    Write info {Importing ...}
    Write info {    Setting up cvs workspace and temporary fossil repository}

    cvs::workspace ; # cd's to workspace
    fossil::new    ; # Uses cwd as workspace to connect to.

    set ntrunk [cvs::ntrunk]
    cvs::foreach_cset cset [cvs::root] {
	import $cset
    }
    cvs::wsclear

    Write info "    ========= [string repeat = 61]"
    Write info "    Imported $nto [expr {($nto == 1) ? "changeset" : "changesets"}]"
    Write info "    Within [format %.2f $tot] seconds (avg [format %.2f [expr {$tot/$nto}]] seconds/changeset)"

    Write info {    Moving to final destination}

    fossil::destination $fossil

    Write info Ok.
    return
}


# -----------------------------------------------------------------------------

proc commandline {} {
    global argv cvs fossil nosign log debugcommit

    set nosign 0
    set debugcommit 0

    while {[string match "-*" [set opt [lindex $argv 0]]]} {
	if {$opt eq "--nosign"} {
	    set nosign 1
	    set argv [lrange $argv 1 end]
	    continue
	}
	if {$opt eq "--debugcommit"} {
	    set debugcommit 1
	    set argv [lrange $argv 1 end]
	    continue
	}
	usage
    }
    if {[llength $argv] != 2} usage
    foreach {cvs fossil} $argv break

    if {
	![file exists      $cvs] ||
	![file readable    $cvs] ||
	![file isdirectory $cvs]
    } {
	usage "CVS directory missing, not readable, or not a directory."
    } elseif {[file exists $fossil]} {
	usage "Fossil destination repository exists already."
    }

    set log [open ${fossil}.log w]

    fossil::debugcommit $debugcommit
    return
}

proc usage {{text {}}} {
    global argv0
    puts stderr "Usage: $argv0 ?--nosign? cvs-repository fossil-rpeository"
    if {$text eq ""} return
    puts stderr "       $text"
    exit
}

proc import {cset} {
    global tot nto nosign ntrunk
    Write info "    Importing $cset [string repeat = [expr {60 - [string length $cset]}]]"
    Write info "        At $nto/$ntrunk ([format %.2f [expr {$nto*100.0/$ntrunk}]]%)"

    set usec [lindex [time {
	foreach {uuid ad rm ch} [fossil::commit cvs2fossil $nosign \
				     [cvs::wssetup $cset] \
				     ::cvs::wsignore] break
    } 1] 0]
    cvs::uuid $cset $uuid

    set sec [expr {$usec/1e6}]
    set tot [expr {$tot + $sec}]
    incr nto

    Write info "        == $uuid +${ad}-${rm}*${ch}"
    Write info "        in $sec seconds"

    set avg [expr {$tot/$nto}]
    set max [expr {$ntrunk * $avg}]
    set rem [expr {$max - $tot}]

    Write info "        st avg [format %.2f $avg]"
    Write info "        st run [format %7.2f $tot] sec [format %6.2f [expr {$tot/60}]] min [format %5.2f [expr {$tot/3600}]] hr"
    Write info "        st end [format %7.2f $max] sec [format %6.2f [expr {$max/60}]] min [format %5.2f [expr {$max/3600}]] hr"
    Write info "        st rem [format %7.2f $rem] sec [format %6.2f [expr {$rem/60}]] min [format %5.2f [expr {$rem/3600}]] hr"
    return
}

# -----------------------------------------------------------------------------

array set fl {
    debug   {DEBUG  }
    info    {       }
    warning {Warning}
    error   {ERROR  }
}

proc Write {l t} {
    global fl log

    if {[string index $t 0] eq "\r"} {
	puts -nonewline stdout "\r$fl($l) [string range $t 0 end-1]"
    } else {
	puts stdout "$fl($l) $t"
	puts $log   "$fl($l) $t"
    }
    flush stdout
    return
}

# -----------------------------------------------------------------------------

main
exit
