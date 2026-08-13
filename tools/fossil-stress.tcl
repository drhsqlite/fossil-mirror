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
    error "unknown argument \"$x\""
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
set useragent {Mozilla/5.0 (fossil-stress.tcl) Gecko/20100101 Firefox/57.0}
set path [string trimright $path /]
set port [string trimleft $port :]
if {$port==""} {set port 80}

proc send_one_request {tid domain port path} {
  while {[catch {
    set x [socket $domain $port]
    fconfigure $x -translation binary -blocking 0
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
  } msg]} {
    puts "ERROR: $msg"
    after 1000
  }
  global cnt stime threadid
  set cnt($x) 0
  set stime($x) [clock seconds]
  set threadid($x) $tid
  flush $x
  fileevent $x readable [list get_reply $tid $path $x]
}

proc close_connection {x} {
  global cnt stime tid
  close $x
  unset -nocomplain cnt($x)
  unset -nocomplain stime($x)
  unset -nocomplain threadid($x)
}

proc get_reply {tid info x} {
  global cnt
  if {[eof $x]} {
    puts "[format %3d: $tid] $info ($cnt($x) bytes)"
    flush stdout
    close_connection $x
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
  /test-env
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

proc unhang_stalled_threads {} {
  global stime threadid
  set now [clock seconds]
  # puts "checking for stalled threads...."
  foreach x [array names stime] {
    # puts -nonewline " $threadid($x)=[expr {$now-$stime($x)}]"
    if {$stime($x)+0<$now-10} {
      set t $threadid($x)
      puts "RESTART thread $t"
      flush stdout
      close_connection $x
      start_another_request $t
    }
  }
  # puts ""
  flush stdout
  after 10000 unhang_stalled_threads
}

unhang_stalled_threads
for {set i 1} {$i<=$nthread} {incr i} {
  start_another_request $i
}
vwait forever
