
# -----------------------------------------------------------------------------
# Parse RCS files (,v) - ignore the deltas - we need only the commit messages
# Recursive Descent Parser

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require fileutil ; # Tcllib (cat)

namespace eval ::rcsparser {}

# -----------------------------------------------------------------------------
# API

proc ::rcsparser::process {path} {
    set data [fileutil::cat -encoding binary $path]
    array set res {}
    Admin
    Deltas
    Description
    DeltaTexts
    return [array get res]
}

# -----------------------------------------------------------------------------
# Internal helper commands

proc ::rcsparser::Admin {} {
    upvar 1 data data res res
    Head ; Branch ; Access ; Symbols ; Locks ; Strict ; Comment ; Expand
    return
}

proc ::rcsparser::Deltas {} {
    upvar 1 data data res res
    while {[Num 0]} { Date ; Author ; State ; Branches ; NextRev }
    return
}

proc ::rcsparser::Description {} {
    upvar 1 data data res res
    Literal desc
    String 1
    Def desc
    return
}

proc ::rcsparser::DeltaTexts {} {
    upvar 1 data data res res
    while {[Num 0]} { Log ; Text }
    return
}

proc ::rcsparser::Head {} {
    upvar 1 data data res res
    Literal head ; Num 1 ; Literal \;
    Def head
    return
}

proc ::rcsparser::Branch {} {
    upvar 1 data data res res
    if {![Literal branch 0]} return ; Num 1 ; Literal \;
    Def branch
    return
}

proc ::rcsparser::Access {} {
    upvar 1 data data res res
    Literal access ; Literal \;
    return
}

proc ::rcsparser::Symbols {} {
    upvar 1 data data res res
    Literal symbols
    while {[Ident]} { Num 1 ; Map symbol }
    Literal \;
    return
}

proc ::rcsparser::Locks {} {
    upvar 1 data data res res
    Literal locks
    while {[Ident]} { Num 1 ; Map lock }
    Literal \;
    return
}

proc ::rcsparser::Strict {} {
    upvar 1 data data res res
    if {![Literal strict 0]} return ; Literal \;
    return
}

proc ::rcsparser::Comment {} {
    upvar 1 data data res res
    if {![Literal comment 0]} return ;
    if {![String 0]} return ;
    Literal \;
    Def comment
    return
}

proc ::rcsparser::Expand {} {
    upvar 1 data data res res
    if {![Literal expand 0]} return ;
    if {![String 0]} return ;
    Literal \;
    Def expand
    return
}

proc ::rcsparser::Date {} {
    upvar 1 data data res res
    Literal date ; Num 1 ; Literal \;
    return
}

proc ::rcsparser::Author {} {
    upvar 1 data data res res
    Literal author ; Skip ; Literal \;
    return
}

proc ::rcsparser::State {} {
    upvar 1 data data res res
    Literal state ; Skip ; Literal \;
    return
}

proc ::rcsparser::Branches {} {
    upvar 1 data data res res
    Literal branches ; Skip ; Literal \;
    return
}

proc ::rcsparser::NextRev {} {
    upvar 1 data data res res
    Literal next ; Skip ; Literal \;
    return
}

proc ::rcsparser::Log {} {
    upvar 1 data data res res
    IsIdent ; Literal log ; String 1 ; Map commit
    return
}

proc ::rcsparser::Text {} {
    upvar 1 data data res res
    Literal text ; String 1
    return
}

proc ::rcsparser::Ident {} {
    upvar 1 data data res res

    #puts I@?<[string range $data 0 10]...>

    if {[regexp -indices -- {^\s*;\s*} $data]} {
	return 0
    } elseif {![regexp -indices -- {^\s*([^:]*)\s*:\s*} $data match val]} {
	return 0
    }

    Get $val ; IsIdent
    Next
    return 1
}

proc ::rcsparser::Literal {name {required 1}} {
    upvar 1 data data res res
    if {![regexp -indices -- "^\\s*$name\\s*" $data match]} {
	if {$required} {
	    return -code error "Expected '$name' @ '[string range $data 0 30]...'"
	}
	return 0
    }

    Next
    return 1
}

proc ::rcsparser::String {{required 1}} {
    upvar 1 data data res res

    if {![regexp -indices -- {^\s*@(([^@]*(@@)*)*)@\s*} $data match val]} {
	if {$required} {
	    return -code error "Expected string @ '[string range $data 0 30]...'"
	}
	return 0
    }

    Get $val
    Next
    return 1
}

proc ::rcsparser::Num {required} {
    upvar 1 data data res res
    if {![regexp -indices -- {^\s*((\d|\.)+)\s*} $data match val]} {
	if {$required} {
	    return -code error "Expected id @ '[string range $data 0 30]...'"
	}
	return 0
    }

    Get $val
    Next
    return 1
}

proc ::rcsparser::Skip {} {
    upvar 1 data data res res
    regexp -indices -- {^\s*[^;]*\s*} $data match
    Next
    return
}

proc ::rcsparser::Def {key} {
    upvar 1 data data res res
    set res($key) $res(lastval)
    unset res(lastval)
    return
}

proc ::rcsparser::Map {key} {
    upvar 1 data data res res
    lappend res($key) $res(id) $res(lastval)
    #puts Map($res(id))=($res(lastval))
    unset res(lastval)
    unset res(id)
    return
}

proc ::rcsparser::IsIdent {} {
    upvar 1 data data res res
    set res(id) $res(lastval)
    unset res(lastval)
    return
}

proc ::rcsparser::Get {val} {
    upvar 1 data data res res
    foreach {s e} $val break
    set res(lastval) [string range $data $s $e]
    #puts G|$res(lastval)
    return
}

proc ::rcsparser::Next {} {
    upvar 1 match match data data
    foreach {s e} $match break ; incr e
    set data [string range $data $e end]
    return
}

# -----------------------------------------------------------------------------
# Ready

package provide rcsparser 1.0
return
