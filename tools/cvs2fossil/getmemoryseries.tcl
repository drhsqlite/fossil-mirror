#!/bin/bash
# -*- tcl -*- \
exec tclsh "$0" ${1+"$@"}

package require csv
foreach {in outbasic outmarker plot outbasicold} $argv break

set in [open $in        r]
set ba [open $outbasic  w]
set mr [open $outmarker w]

puts $ba "\# Time Memory MaxMemory"
puts $mr "\# Time Memory"

set k 0
while {![eof $in]} {
    gets $in line
    #puts -nonewline \r[incr k]

    if {[string match *|=|* $line]} {
	# Basic series
	regexp {^(.*)\|=\|} $line -> line
	foreach {x _ cba _ _ _ mba} $line break
	puts $ba [join [list $x $cba $mba] \t]
	continue
    }

    if {[string match *|@|* $line]} {
	# Marker series
	regexp {^(.*)\|@\|} $line -> line
	foreach {x _ cba} $line break
	puts $mr [join [list $x $cba] \t]
	continue
    }
}

puts ""
close $in
close $ba
close $mr

# Generate gnuplot control file for the series
set    f [open $plot w]
puts  $f ""
puts  $f "plot \"$outbasic\" using 1:2 title 'Memory'     with steps, \\"
puts  $f "     \"$outbasic\" using 1:3 title 'Max Memory' with steps"
puts  $f "pause -1"
puts  $f ""
close $f

# Generate gnuplot control file for comparison of series
set    f [open ${plot}-compare w]
puts  $f ""
puts  $f "plot \"$outbasicold\" using 1:2 title 'Memory Old' with steps, \\"
puts  $f "     \"$outbasic\"    using 1:2 title 'Memory New' with steps"
puts  $f "pause -1"
puts  $f ""
close $f
exit

# Comparison to baseline
plot "basic.dat"     using 1:2 title 'Memory Base'    with steps lt rgb "blue", \
     "newbasic.dat"  using 1:2 title 'Memory Current' with steps lt rgb "red", \

# Comparison to baseline via normalization - need math op (div)
plot "basic.dat"     using 1:2 title 'Memory Base'    with steps lt rgb "blue", \
     "newbasic.dat"  using 1:2 title 'Memory Current' with steps lt rgb "red", \


