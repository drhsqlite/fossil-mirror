# -----------------------------------------------------------------------------
# Tool packages. Logging (aka User feedback).

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
namespace eval ::tools::log {}

# -----------------------------------------------------------------------------
# API

# Feedback generation.
#
#	tools::log::write    verbosity system text  - Write message to the log.
#	tools::log::progress verbosity system n max - Drive a progress display.

# Administrative operations.
#
#	tools::log::verbosity level  - Set the verbosity level of the application.
#	tools::log::verbosity?       - Query the verbosity level of the application.
#	tools::log::setCmd cmdprefix - Set callback for output
#	tools::log::system name      - Register a system (enables tabular log formatting).

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

proc ::tools::log::write {verbosity system text} {
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

proc ::tools::log::progress {verbosity system n max} {
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

proc ::tools::log::verbosity {level} {
    variable loglevel
    if {$level < 1} {set level 0}
    set loglevel $level
    return
}

# Query the currently set verbosity.

proc ::tools::log::verbosity? {} {
    variable loglevel
    return  $loglevel
}

# Set the log callback handling the actual output of messages going
# through the package.

proc ::tools::log::setCmd {cmdprefix} {
    variable logcmd $cmdprefix
    return
}

# Register a system name, to enable tabular formatting. This is done
# by setting up a format specifier with a proper width. This is
# handled in the generation command, before the output callback is
# invoked.

proc ::tools::log::system {name} {
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

proc ::tools::log::OUT {op args} {
    eval [linsert $args 0 ::tools::log::OUT/$op]
    return
}

# Write handler. Each message is a line.

proc ::tools::log::OUT/write {system text} {
    puts "$system $text"
    return
}

# Progress handler. Using \r to return to the beginning of the current
# line without advancing.

proc ::tools::log::OUT/progress {system n max} {
    puts -nonewline "$system [format %[string length $max]s $n]/$max\r"
    flush stdout
    return
}

# -----------------------------------------------------------------------------

namespace eval ::tools::log {
    variable loglevel 0                 ; # Allow only uninteruptible messages.
    variable logcmd   ::tools::log::OUT ; # Standard output to stdout.
    variable sysfmt %s                  ; # Non-tabular formatting.
    variable syslen 0                   ; # Ditto.

    namespace export write progress
}

# -----------------------------------------------------------------------------
# Ready

package provide tools::log 1.0
return
