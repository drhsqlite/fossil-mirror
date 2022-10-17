#!/usr/bin/env tclsh
# man_page_command_list.tcl - generates common command list for fossil.1

# Tunable configuration.
set columns 5
set width 15

# The only supported command-line argument is the optional output filename.
if {[llength $argv] == 1} {
    set file [lindex $argv 0]
}

# Get list of common commands.
set commands [exec fossil help]
regsub -nocase {.*?\ncommon commands:.*\n} $commands {} commands
regsub -nocase {\nthis is fossil version.*} $commands {} commands
regsub -all {\s+} $commands " " commands
set commands [lsort $commands]

# Compute number of rows.
set rows [expr {([llength $commands] + $columns - 1) / $columns}]

# Generate text one line at a time.
set text {}
for {set row 0} {$row < $rows} {incr row} {
    # Separate rows with line break.
    if {$row} {
        append text .br\n
    }

    # Generate the row of commands.
    for {set col 0} {$col < $columns} {incr col} {
        set i [expr {$col * $rows + $row}]
        if {$i < [llength $commands]} {
            append text [format %-*s $width [lindex $commands $i]]
        }
    }
    append text \n
}

# Strip trailing whitespace from each line.
regsub -all {\s+\n} $text \n text

# Output text.
if {[info exists file]} {
    # If a filename was specified, read the file for use as a template.
    set chan [open $file]
    set data [read $chan]
    close $chan

    # Locate the part of the file to replace.
    if {[regexp -indices {\n\.SH Common COMMANDs:\n\n(.*?)\n\.SH} $data\
            _ range]} {
        # If found, replace with the updated command list.
        set chan [open $file w]
        puts -nonewline $chan [string replace $data\
                [lindex $range 0] [lindex $range 1] $text]
        close $chan
    } else {
        # If not found, abort.
        error "could not find command list in man file \"$file\""
    }
} else {
    # If no filename was specified, write to stdout.
    puts $text
}

# vim: set sts=4 sw=4 tw=80 et ft=tcl:
