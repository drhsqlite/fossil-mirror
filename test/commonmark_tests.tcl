package require rl_json
package require tdom

set f [open "commonmark_tests.json"]
fconfigure $f -encoding utf-8
set data [read $f]
close $f

set tests [rl_json::json parse $data]

proc normalize {html} {
    if {![catch {[[dom parse -html "<html>$html</html>"] documentElement] asHTML} res]} {
	return [string range $res 6 end-7]
    } else {
	return $html
    }
}

set errored 0
set failed 0
set passed 0
foreach test $tests {
    dict with test {
        puts ---------------
	puts -nonewline "Test: $example"
	set f [open "in.md" w+]
	fconfigure $f -encoding utf-8
	puts $f [subst -nocommands -novariable $markdown]\n
	close $f
	set expected [normalize [subst -nocommands -novariables [string trim $html]]]
	if {[catch {
	    set data [exec ../fossil test-markdown-render in.md]
	    
	    # remove pre/post amble
	    set data [split $data \n]
	    set data [lrange $data 1 end-2]
	    set result [normalize [string trim [join $data \n]]]
	} err]} {
	    incr errored
	    set result "crashed"
            puts stderr $err
	    puts "crashed"
        } elseif {$result ne $expected} {
	    incr failed
	    puts " failed, -: expected, +: actual:"
	    foreach line [split $expected \n] { puts "-:$line" }
	    foreach line [split $result \n] { puts "+:$line" }
        } else { 
	    incr passed
	    puts " passed"
	    foreach line [split $expected \n] { puts "-:$line" }
	    foreach line [split $result \n] { puts "+:$line" }
	}
    }
}

puts "Passed: $passed, failed: $failed, errored: $errored"
file delete in.md
