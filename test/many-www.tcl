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
} {
  set seen($url) 1
  set pending($url) 1
}
set limit 10000
set npending [llength [array names pending]]
proc get_pending {} {
  global pending npending
  set res [lindex [array names pending] [expr {int(rand()*$npending)}]]
  unset pending($res)
  incr npending -1
  return $res
}
for {set i 0} {$npending>0 && $i<$limit} {incr i} {
  set url [get_pending]
  puts -nonewline "([expr {$i+1}]) $url "
  flush stdout
  set tm [time {set x [run_query $url]}]
  set ms [lindex $tm 0]
  puts [format {%.3fs} [expr {$ms/1000000.0}]]
  flush stdout
  if {[string length $x]>1000000} {
    set x [string range $x 0 1000000]
  }
  while {[regexp {<[aA] .*?href="(/[a-z].*?)".*?>(.*)$} $x all url tail]} {
    # if {$npending>2*($limit - $i)} break
    set u2 [string map {&lt; < &gt; > &quot; \" &amp; &} $url]
    if {![info exists seen($u2)]} {
      set pending($u2) 1
      set seen($u2) 1
      incr npending
    }
    set x $tail
  }
}
