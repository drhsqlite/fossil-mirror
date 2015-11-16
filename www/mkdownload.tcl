#!/usr/bin/tclsh
#
# Run this script to build the "download.html" page.  Also generate
# the fossil_download_checksums.html page.
#
#
set out [open download.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out \
{<!DOCTYPE html>
<html>
  <head>
    <base href="https://www.fossil-scm.org/download.html" />
    <title>Fossil: Download</title>
      <link rel="alternate" type="application/rss+xml" title="RSS Feed"
            href="/fossil/timeline.rss" />
      <link rel="stylesheet" href="/fossil/style.css?default" type="text/css"
            media="screen" />
  </head>

  <body>
    <div class="header">
      <div class="title"><h1>Fossil</h1>Download</div>
    </div>
    <div class="mainmenu">
<a href='/fossil/doc/trunk/www/index.wiki'>Home</a>
<a href='/fossil/timeline?y=ci'>Timeline</a>
<a href='/fossil/dir?ci=tip'>Code</a>
<a href='/fossil/doc/trunk/www/permutedindex.html'>Docs</a>
<a href='/fossil/brlist'>Branches</a>
<a href='/fossil/ticket'>Tickets</a>
<a href='/fossil/wiki'>Wiki</a>
<a href='/download.html' class='active'>Download</a>
</div>
<div class="content">
<p style="font-size:1.2em; text-align:center">}
puts $out \
"<b>To install Fossil &rarr;</b> download the stand-alone executable"
puts $out \
{and put it on your $PATH.
</p>
<p style="text-align:center">
RPMs available
<a href="http://download.opensuse.org/repositories/home:/rmax:/fossil/">
here.</a>
Cryptographic checksums for download files are
<a href="http://www.hwaci.com/fossil_download_checksums.html">here</a>.
</p>
<hr>
<table cellpadding="10">
}

# Find all all unique timestamps.
#
foreach file [glob -nocomplain download/fossil-*.zip] {
  if {[regexp -- {-(\d\.\d+).zip$} $file all version]} {
    set avers($version) 1
  }
}

# Do all versions from newest to oldest
#
foreach vers [lsort -decr -real [array names avers]] {
  set hr "/fossil/timeline?c=version-$vers;y=ci"
  puts $out "<tr><td colspan=6 align=center>"
  puts $out "<b><a href=\"$hr\">Version $vers</a></b>"
  puts $out "</td></tr>"
  puts $out "<tr>"

  foreach {prefix suffix img desc} {
    fossil-linux-x86 zip linux.gif {Linux 3.x x86}
    fossil-macosx-x86 zip mac.gif {Mac 10.x x86}
    fossil-openbsd-x86 zip openbsd.gif {OpenBSD 5.x x86}
    fossil-w32 zip win32.gif {Windows}
    fossil-src tar.gz src.gif {Source Tarball}
  } {
    set filename download/$prefix-$vers.$suffix
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
  if {[file exists download/releasenotes-$vers.html]} {
    puts $out "<tr><td colspan=6 align=left>"
    set rn [open download/releasenotes-$vers.html]
    fconfigure $rn -encoding utf-8
    puts $out "[read $rn]"
    close $rn
    puts $out "</td></tr>"
  }
}
puts $out "<tr><td colspan=5><hr></td></tr>"

puts $out {</table></div>
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
<a href="/download.html">Fossil website</a>.</p>
<pre>}

foreach file [lsort [glob -nocomplain download/fossil-*.zip]] {
  set sha1sum [lindex [exec sha1sum $file] 0]
  puts $out "$sha1sum   [file tail $file]"
}
puts $out {</pre></body></html>}
close $out
