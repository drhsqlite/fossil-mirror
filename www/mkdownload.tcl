#!/usr/bin/tclsh
#
# Run this script to build the "download.html" page on standard output.
#
#
puts \
{<html>
<head>
<title>Fossil: Downloads</title>
<link rel="stylesheet" href="/fossil/style.css" type="text/css"
      media="screen">
</head>
<body>
<div class="header">
  <div class="logo">
    <img src="/fossil/doc/tip/www/fossil_logo_small.gif" alt="logo">
  </div>
  <div class="title">Fossil Downloads</div>
</div>
<div class="mainmenu"><a href='/fossil/doc/tip/www/index.wiki'>Home</a><a href='/fossil/timeline'>Timeline</a><a href='/fossil/brlist'>Branches</a><a href='/fossil/taglist'>Tags</a><a href='/fossil/reportlist'>Tickets</a><a href='/fossil/wiki'>Wiki</a><a href='/fossil/login'>Login</a></div>
<div class="content">
<p>

<p>
Click on links below to download prebuilt binaries and source tarballs for 
recent versions of <a href="/fossil">Fossil</a>.
The historical source code is also available in the
<a href="/fossil/doc/tip/www/selfhost.wiki">self-hosting
Fossil repositories</a>.
</p>

<table cellpadding="10">
}

# Find all all unique timestamps.
#
foreach file [glob -nocomplain download/fossil-*.zip] {
  if {[regexp {(\d+).zip$} $file all datetime]
       && [string length $datetime]>=14} {
    set adate($datetime) 1
  }
}

# Do all dates from newest to oldest
#
foreach datetime [lsort -decr [array names adate]] {
  set dt [string range $datetime 0 3]-[string range $datetime 4 5]-
  append dt "[string range $datetime 6 7] "
  append dt "[string range $datetime 8 9]:[string range $datetime 10 11]:"
  append dt "[string range $datetime 12 13]"
  set link [string map {{ } +} $dt]
  set hr http://www.fossil-scm.org/fossil/timeline?c=$link&y=ci
  puts "<tr><td colspan=5 align=center><hr>"
  puts "<b>Fossil snapshot as of <a href=\"$hr\">$dt</a><td width=30></b>"
  puts "</td></tr>"
  
  foreach {prefix suffix img desc} {
    fossil-linux-x86 zip linux.gif {Linux x86}
    fossil-linux-amd64 zip linux64.gif {Linux x86_64}
    fossil-macosx-x86 zip mac.gif {Mac 10.5 x86}
    fossil-openbsd-x86 zip openbsd.gif {OpenBSD 4.7 x86}
    fossil-w32 zip win32.gif {Windows}
    fossil-src tar.gz src.gif {Source Tarball}
  } {
    set filename download/$prefix-$datetime.$suffix
    if {[file exists $filename]} {
      set size [file size $filename]
      set units bytes
      if {$size>1024*1024} {
        set size [format %.2f [expr {$size/(1024.0*1024.0)}]]
        set units MiB
      } elseif {$size>1024} {
        set size [format %.2f [expr {$size/(1024.0)}]]
        set units KiB
      }
      puts "<td align=center valign=bottom><a href=\"$filename\">"
      puts "<img src=\"build-icons/$img\" border=0><br>$desc</a><br>"
      puts "$size $units</td>"
    } else {
      puts "<td>&nbsp;</td>"
    }
  }
  puts "</tr>"
}
puts "<tr><td colspan=5><hr></td></tr>"

puts {</table>
</body>
</html>
}
