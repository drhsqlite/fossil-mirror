#!/usr/bin/tclsh
#
# Run this script to build the "download.html" page.  Also generate
# the fossil_download_checksums.html page.
#
#
set out [open download.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out \
{<!DOCTYPE html><html>
<head>
<base href="http://www.fossil-scm.org/" />
<title>Fossil: Timeline</title>
<link rel="stylesheet" href="/fossil/style.css" type="text/css"
      media="screen">
</head>
<body>
<div class="header">
  <div class="logo">
    <img src="/fossil/logo" alt="logo">
    <br /><nobr>Fossil</nobr>
  </div>

  <div class="title">Fossil Downloads</div>
</div>
<div class="mainmenu">
<a href='/fossil/doc/trunk/www/index.wiki'>Home</a>
<a href='/fossil/timeline?y=ci'>Timeline</a>
<a href='/download.html'>Download</a>
<a href='/fossil/dir?ci=trunk'>Code</a>
<a href='/fossil/doc/trunk/www/permutedindex.wiki'>Documentation</a>
<a href='/fossil/brlist'>Branches</a>
<a href='/fossil/taglist'>Tags</a>
<a href='/fossil/reportlist'>Tickets</a>
</div>
<div class="content">
<p>

<center><font size=4>}
puts $out \
"<b>To install Fossil \u2192</b> download the stand-alone executable"
puts $out \
{and put it on your $PATH.
</font><p><small>
RPMs available
<a href="http://download.opensuse.org/repositories/home:/rmax:/fossil/">
here.</a>
Cryptographic checksums for download files are
<a href="http://www.hwaci.com/fossil_download_checksums.html">here</a>.
</small></p>
</center>

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
  set hr http://www.fossil-scm.org/fossil/timeline?c=$link&amp;y=ci
  puts $out "<tr><td colspan=6 align=left><hr>"
  puts $out "<center><b><a href=\"$hr\">$dt</a></b></center>"
  puts $out "</td></tr>"
  puts $out "<tr>"

  foreach {prefix suffix img desc} {
    fossil-linux-x86 zip linux.gif {Linux x86}
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
      puts $out "<td align=center valign=bottom><a href=\"$filename\">"
      puts $out "<img src=\"build-icons/$img\" border=0><br>$desc</a><br>"
      puts $out "$size $units</td>"
    } else {
      puts $out "<td>&nbsp;</td>"
    }
  }
  puts $out "</tr>"
  if {[file exists download/releasenotes-$datetime.html]} {
    puts $out "<tr><td colspan=6 align=left>"
    set rn [open download/releasenotes-$datetime.html]
    fconfigure $rn -encoding utf-8
    puts $out "[read $rn]"
    close $rn
    puts $out "</td></tr>"
  }
}
puts $out "<tr><td colspan=5><hr></td></tr>"

puts $out {</table>
</body>
</html>
}

close $out

# Generate the checksum page
#
set out [open fossil_download_checksums.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out {<html>
<title>Fossil Download Checksums</title>
<body>
<h1 align="center">Checksums For Fossil Downloads</h1>
<p>The following table shows the SHA1 checksums for the precompiled
binaries available on the
<a href="http://www.fossil-scm.org/download.html">Fossil website</a>.</p>
<pre>}

foreach file [lsort [glob -nocomplain download/fossil-*.zip]] {
  set sha1sum [lindex [exec sha1sum $file] 0]
  puts $out "$sha1sum   [file tail $file]"
}
puts $out {</pre></body></html>}
close $out
