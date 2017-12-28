#!/usr/bin/tclsh
#
# Run this script, giving the url of a Fossil server instances as the
# argument, and this script will start sending HTTP requests into the
# that server instance as fast as it can, as a stress test for the
# server implementation.
#
set url [lindex $argv 0]
if {$url==""} {
  error "Usage: $argv0 URL"
}
if {![regexp {^https?://([^/:]+)(:\d+)?(/.*)$} $url all domain port path]} {
  error "could not parse the URL [list $url] -- should be of the\
         form \"http://domain/path\""
}
set useragent {Mozilla/5.0 (X11; Linux x86_64; rv:57.0) Gecko/20100101 Firefox/57.0}
set path [string trimright $path /]
set port [string trimleft $port :]
if {$port==""} {set port 80}

proc send_one_request {domain port path} {
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
  set cnt 0
  while {![eof $x]} {
    incr cnt [string length [read $x]]
  }
  close $x
  return $cnt
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

set cnt 0
while {1} {
  foreach p $pages {
    incr cnt
    puts -nonewline "$cnt: $path$p... "
    flush stdout
    set n [send_one_request $domain $port $path$p]
    puts "$n bytes"
  }
}
