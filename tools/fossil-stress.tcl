#!/usr/bin/tclsh
#
# Run this script, giving the url of a Fossil server instances as the
# argument, and this script will start sending HTTP requests into the
# that server instance as fast as it can, as a stress test for the
# server implementation.
#
set nthread 10
for {set i 0} {$i<[llength $argv]} {incr i} {
  set x [lindex $argv $i]
  if {[regexp {^--[a-z]} $x]} {
    set x [string range $x 1 end]
  }
  if {$x=="-threads"} {
    incr i
    set nthread [lindex $argv $i]
  } elseif {[string index $x 0]=="-"} {
    error "unknown option \"$x\""
  } elseif {[info exists url]} {
    error "unknown argment \"$x\""
  } else {
    set url $x
  }
}
if {![info exists url]} {
  error "Usage: $argv0 [-threads N] URL"
}
if {![regexp {^https?://([^/:]+)(:\d+)?(/.*)$} $url all domain port path]} {
  error "could not parse the URL [list $url] -- should be of the\
         form \"http://domain/path\""
}
set useragent {Mozilla/5.0 (X11; Linux x86_64; rv:57.0) Gecko/20100101 Firefox/57.0}
set path [string trimright $path /]
set port [string trimleft $port :]
if {$port==""} {set port 80}

proc send_one_request {tid domain port path} {
  set x [socket $domain $port]
  fconfigure $x -translation binary
  puts $x "GET $path HTTP/1.0\r"
  if {$port==80} {
    puts $x "Host: $domain\r"
  } else {
    puts $x "Host: $domain:$port\r"
  }
  puts $x "User-Agent: $::useragent\r"
  puts $x "Accept: text/html,q=0.9,*/*;q=0.8\r"
  puts $x "Accept-Language: en-US,en;q=0.5\r"
  puts $x "Connection: close\r"
  puts $x "\r"
  flush $x
  global cnt
  set cnt($x) 0
  fconfigure $x -blocking 0
  fileevent $x readable [list get_reply $tid $path $x]
}

proc get_reply {tid info x} {
  global cnt
  if {[eof $x]} {
    puts "[format %3d: $tid] $info ($cnt($x) bytes)"
    flush stdout
    close $x
    unset cnt($x)
    start_another_request $tid
  } else {
    incr cnt($x) [string length [read $x]]
  }
}

set pages {
  /timeline?n=20
  /timeline?n=20&a=1970-01-01
  /home
  /brlist
  /info/trunk
  /info/2015-01-01
  /vdiff?from=2015-01-01&to=trunk&diff=0
  /wcontent
  /fileage
  /dir
  /tree
  /uvlist
  /stat
  /test_env
  /sitemap
  /hash-collisions
  /artifact_stats
  /bloblist
  /bigbloblist
  /wiki_rules
  /md_rules
  /help
  /test-all-help
  /timewarps
  /taglist
}

set pageidx 0
proc start_another_request {tid} {
  global pages pageidx domain port path
  set p [lindex $pages $pageidx]
  incr pageidx
  if {$pageidx>=[llength $pages]} {set pageidx 0}
  send_one_request $tid $domain $port $path$p
}

for {set i 1} {$i<=$nthread} {incr i} {
  start_another_request $i
}
vwait forever
