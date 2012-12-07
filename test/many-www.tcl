#!/usr/bin/tclsh
#
# Run this script from within any open Fossil checkout.  Example:
#
#   tclsh many-www.tcl | tee out.txt
#
# About 10,000 different web page requests will be made.  Each is timed
# and the time shown on output. Use this script to search for segfault problems
# or to look for pages that need optimization.
#
proc run_query {url} {
  set fd [open q.txt w]
  puts $fd "GET $url HTTP/1.0\r\n\r"
  close $fd
  return [exec fossil test-http <q.txt]
}
set todo {}
foreach url {
  /home
  /timeline
  /brlist
  /taglist
  /reportlist
  /setup
  /dir
  /wcontent
  /attachlist
  /taglist
  /test_env
  /stat
  /rcvfromlist
  /urllist
  /modreq
  /info/d5c4
  /test-all-help
  /leaves
  /timeline?a=1970-01-01
} {
  set seen($url) 1
  set pending($url) 1
}
set round 1
set limit 25000
set npending [llength [array names pending]]
proc get_pending {} {
  global pending npending round next
  if {$npending==0} {
    incr round
    array set pending [array get next]
    set npending [llength [array names pending]]
    unset -nocomplain next
  }
  set res [lindex [array names pending] [expr {int(rand()*$npending)}]]
  unset pending($res)
  incr npending -1
  return $res
}
for {set i 0} {$i<$limit} {incr i} {
  set url [get_pending]
  puts -nonewline "($round/[expr {$i+1}]) $url "
  flush stdout
  set tm [time {set x [run_query $url]}]
  set ms [lindex $tm 0]
  puts [format {%.3fs} [expr {$ms/1000000.0}]]
  flush stdout
  if {[string length $x]>1000000} {
    set x [string range $x 0 1000000]
  }
  set k 0
  while {[regexp {<[aA] .*?href="(/[a-z].*?)".*?>(.*)$} $x all url tail]} {
    # if {$npending>2*($limit - $i)} break
    incr k
    if {$k>100} break
    set u2 [string map {&lt; < &gt; > &quot; \" &amp; &} $url]
    if {![info exists seen($u2)]} {
      set next($u2) 1
      set seen($u2) 1
    }
    set x $tail
  }
}
