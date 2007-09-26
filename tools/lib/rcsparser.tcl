# -----------------------------------------------------------------------------
# Tool packages. Parsing RCS files.
#
# Some of the information in RCS files is skipped over, most
# importantly the actual delta texts. The users of this parser need
# only the meta-data about when revisions were added, the tree
# (branching) structure, commit messages.
#
# The parser is based on Recursive Descent.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require fileutil       ; # Tcllib (cat)
package require vc::tools::log ; # User feedback

namespace eval ::vc::rcs::parser {
    vc::tools::log::system rcs
    namespace import ::vc::tools::log::*
}

# -----------------------------------------------------------------------------
# API

# vc::rcs::parser::process file
#
# Parses the rcs file and returns a dictionary containing the meta
# data. The following keys are used
#
# Key		Meaning
# ---		-------
# 'head'	head revision
# 'branch'	?
# 'symbol'	dict (symbol -> revision)
# 'lock'	dict (symbol -> revision)
# 'comment'	file comment
# 'expand'	?
# 'date'	dict (revision -> date)
# 'author'	dict (revision -> author)
# 'state'	dict (revision -> state)
# 'parent'	dict (revision -> parent revision)
# 'commit'	dict (revision -> commit message)
#
# The state 'dead' has special meaning, the user should know that.

# -----------------------------------------------------------------------------
# API Implementation

proc ::vc::rcs::parser::configure {key value} {
    variable cache
    switch -exact -- $key {
	-cache  {
	    set cache $value
	}
	default {
	    return -code error "Unknown switch $key, expected one of -cache"
	}
    }
    return
}

proc ::vc::rcs::parser::process {path} {
    set cache [Cache $path]
    if {
	[file exists $cache] &&
	([file mtime $cache] > [file mtime $path])
    } {
	# Use preparsed data if not invalidated by changes to the
	# archive they are derived from.
	write 4 rcs {Load preparsed data block}
	return [fileutil::cat -encoding binary $cache]
    }

    set res [Process $path]

    # Save parse result for quick pickup by future runs.
    fileutil::writeFile $cache $res

    return $res
}

# -----------------------------------------------------------------------------

proc ::vc::rcs::parser::Process {path} {
    set data [fileutil::cat -encoding binary $path]
    array set res {}
    set res(size) [file size $path]
    set res(done) 0
    set res(nsize) [string length $res(size)]

    Admin
    Deltas
    Description
    DeltaTexts

    # Remove parser state
    catch {unset res(id)}
    catch {unset res(lastval)}
    unset res(size)
    unset res(nsize)
    unset res(done)

    return [array get res]
}

proc ::vc::rcs::parser::Cache {path} {
    return ${path},,preparsed
}

# -----------------------------------------------------------------------------
# Internal - Recursive Descent functions implementing the syntax.

proc ::vc::rcs::parser::Admin {} {
    upvar 1 data data res res
    Head ; Branch ; Access ; Symbols ; Locks ; Strict ; Comment ; Expand
    return
}

proc ::vc::rcs::parser::Deltas {} {
    upvar 1 data data res res
    while {[Num 0]} { IsIdent ; Date ; Author ; State ; Branches ; NextRev }
    return
}

proc ::vc::rcs::parser::Description {} {
    upvar 1 data data res res
    Literal desc
    String 1
    Def desc
    return
}

proc ::vc::rcs::parser::DeltaTexts {} {
    upvar 1 data data res res
    while {[Num 0]} { IsIdent ; Log ; Text }
    return
}

proc ::vc::rcs::parser::Head {} {
    upvar 1 data data res res
    Literal head ; Num 1 ; Literal \;
    Def head
    return
}

proc ::vc::rcs::parser::Branch {} {
    upvar 1 data data res res
    if {![Literal branch 0]} return ; Num 1 ; Literal \;
    Def branch
    return
}

proc ::vc::rcs::parser::Access {} {
    upvar 1 data data res res
    Literal access ; Literal \;
    return
}

proc ::vc::rcs::parser::Symbols {} {
    upvar 1 data data res res
    Literal symbols
    while {[Ident]} { Num 1 ; Map symbol }
    Literal \;
    return
}

proc ::vc::rcs::parser::Locks {} {
    upvar 1 data data res res
    Literal locks
    while {[Ident]} { Num 1 ; Map lock }
    Literal \;
    return
}

proc ::vc::rcs::parser::Strict {} {
    upvar 1 data data res res
    if {![Literal strict 0]} return ; Literal \;
    return
}

proc ::vc::rcs::parser::Comment {} {
    upvar 1 data data res res
    if {![Literal comment 0]} return ;
    if {![String 0]} return ;
    Literal \;
    Def comment
    return
}

proc ::vc::rcs::parser::Expand {} {
    upvar 1 data data res res
    if {![Literal expand 0]} return ;
    if {![String 0]} return ;
    Literal \;
    Def expand
    return
}

proc ::vc::rcs::parser::Date {} {
    upvar 1 data data res res
    Literal date ; Num 1 ; Literal \;

    foreach {yr mo dy h m s} [split $res(lastval) .] break
    if {$yr < 100} {incr yr 1900}
    set res(lastval) [join [list $yr $mo $dy $h $m $s] .]
    Map date
    return
}

proc ::vc::rcs::parser::Author {} {
    upvar 1 data data res res
    Literal author ; Skip ; Literal \; ; Map author
    return
}

proc ::vc::rcs::parser::State {} {
    upvar 1 data data res res
    Literal state ; Skip ; Literal \; ; Map state
    return
}

proc ::vc::rcs::parser::Branches {} {
    upvar 1 data data res res
    Literal branches ; Skip ; Literal \;
    return
}

proc ::vc::rcs::parser::NextRev {} {
    upvar 1 data data res res
    Literal next ; Skip ; Literal \; ; Map parent
    return
}

proc ::vc::rcs::parser::Log {} {
    upvar 1 data data res res
    Literal log ; String 1 ; Map commit
    return
}

proc ::vc::rcs::parser::Text {} {
    upvar 1 data data res res
    Literal text ; String 1
    return
}

# -----------------------------------------------------------------------------
# Internal - Lexicographical commands and data aquisition preparation

proc ::vc::rcs::parser::Ident {} {
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

proc ::vc::rcs::parser::Literal {name {required 1}} {
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

proc ::vc::rcs::parser::String {{required 1}} {
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

proc ::vc::rcs::parser::Num {required} {
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

proc ::vc::rcs::parser::Skip {} {
    upvar 1 data data res res
    regexp -indices -- {^\s*([^;]*)\s*} $data match val
    Get $val
    Next
    return
}

# -----------------------------------------------------------------------------
# Internal - Data aquisition

proc ::vc::rcs::parser::Def {key} {
    upvar 1 data data res res
    set res($key) $res(lastval)
    unset res(lastval)
    return
}

proc ::vc::rcs::parser::Map {key} {
    upvar 1 data data res res
    lappend res($key) $res(id) $res(lastval)
    #puts Map($res(id))=($res(lastval))
    unset res(lastval)
    #unset res(id);#Keep id for additional mappings.
    return
}

proc ::vc::rcs::parser::IsIdent {} {
    upvar 1 data data res res
    set res(id) $res(lastval)
    unset res(lastval)
    return
}

proc ::vc::rcs::parser::Get {val} {
    upvar 1 data data res res
    foreach {s e} $val break
    set res(lastval) [string range $data $s $e]
    #puts G|$res(lastval)
    return
}

proc ::vc::rcs::parser::Next {} {
    upvar 1 match match data data res res
    foreach {s e} $match break ; incr e
    set data [string range $data $e end]
    set res(done) [expr {$res(size) - [string length $data]}]

    progress 2 rcs $res(done) $res(size)
    return
}

# -----------------------------------------------------------------------------

namespace eval ::vc::rcs::parser {
    variable cache 0 ; # No result caching by default.

    namespace export process configure
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::rcs::parser 1.0
return
