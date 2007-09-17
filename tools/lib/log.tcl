# -----------------------------------------------------------------------------
# Tool packages. Logging (aka User feedback).

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
namespace eval ::vc::tools::log {}

# -----------------------------------------------------------------------------
# API

# Feedback generation.
#
#	vc::tools::log::write    verbosity system text  - Write message to the log.
#	vc::tools::log::progress verbosity system n max - Drive a progress display.
#
#       Note: max empty => infinite progress display, otherwise a finite display.

# Administrative operations.
#
#	vc::tools::log::verbosity level  - Set the verbosity level of the application.
#	vc::tools::log::verbosity?       - Query the verbosity level of the application.
#	vc::tools::log::setCmd cmdprefix - Set callback for output
#	vc::tools::log::system name      - Register a system (enables tabular log formatting).

# Callback API ( Executed at the global level).
#
#	cmdprefix 'write'    system text
#	cmdprefix 'progress' system n max

# Standard callbacks defined by the package itself write to stdout.

# -----------------------------------------------------------------------------
# API Implementation - Feedback generation.

# Write the message 'text' to log, for the named 'system'. The message
# is written if and only if the message verbosity is less or equal the
# chosen verbosity. A message of verbosity 0 cannot be blocked.

proc ::vc::tools::log::write {verbosity system text} {
    variable loglevel
    variable logcmd
    variable sysfmt
    if {$verbosity > $loglevel} return
    uplevel #0 [linsert $logcmd end write [format $sysfmt $system] $text]
    return
}

# Similar to write, especially in the handling of the verbosity, to
# drive progress displays. It signals that for some long running
# operation we are at tick 'n' of at most 'max' ticks.

proc ::vc::tools::log::progress {verbosity system n max} {
    variable loglevel
    variable logcmd
    variable sysfmt
    if {$verbosity > $loglevel} return
    uplevel #0 [linsert $logcmd end progress [format $sysfmt $system] $n $max]
    return
}

# -----------------------------------------------------------------------------
# API Implementation - Administrative operations.

# Set verbosity to the chosen 'level'. Only messages with a level less
# or equal to this one will be shown.

proc ::vc::tools::log::verbosity {level} {
    variable loglevel
    if {$level < 1} {set level 0}
    set loglevel $level
    return
}

# Query the currently set verbosity.

proc ::vc::tools::log::verbosity? {} {
    variable loglevel
    return  $loglevel
}

# Set the log callback handling the actual output of messages going
# through the package.

proc ::vc::tools::log::setCmd {cmdprefix} {
    variable logcmd $cmdprefix
    return
}

# Register a system name, to enable tabular formatting. This is done
# by setting up a format specifier with a proper width. This is
# handled in the generation command, before the output callback is
# invoked.

proc ::vc::tools::log::system {name} {
    variable sysfmt
    variable syslen

    set nlen [string length $name]
    if {$nlen < $syslen} return

    set syslen $nlen
    set sysfmt %-${syslen}s
    return
}

# -----------------------------------------------------------------------------
# Internal operations - Standard output operation

# Dispatch to the handlers of the possible operations.

proc ::vc::tools::log::OUT {op args} {
    eval [linsert $args 0 ::vc::tools::log::OUT/$op]
    return
}

# Write handler. Each message is a line.

proc ::vc::tools::log::OUT/write {system text} {
    puts "$system $text"
    return
}

# Progress handler. Uses \r to return to the beginning of the current
# line without advancing.

proc ::vc::tools::log::OUT/progress {system n max} {
    if {$max eq {}} {
	puts -nonewline "$system $n\r"
    } else {
	puts -nonewline "$system [format %[string length $max]s $n]/$max\r"
    }
    flush stdout
    return
}

# -----------------------------------------------------------------------------

namespace eval ::vc::tools::log {
    variable loglevel 0                     ; # Allow only uninteruptible messages.
    variable logcmd   ::vc::tools::log::OUT ; # Standard output to stdout.
    variable sysfmt %s                      ; # Non-tabular formatting.
    variable syslen 0                       ; # Ditto.

    namespace export write progress
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::tools::log 1.0
return
