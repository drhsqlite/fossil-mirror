#!/usr/bin/tclsh
#
# Run this script to build and install the "download.html" page of
# unversioned comment.
#
# Also generate the fossil_download_checksums.html page.
#
#
set out [open download.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out \
{<div class='fossil-doc' data-title='Download Page'>

<center><font size=4>}
puts $out \
"<b>To install Fossil &rarr;</b> download the stand-alone executable"
puts $out \
{and put it on your $PATH.
</font><p><small>
RPMs available
<a href="http://download.opensuse.org/repositories/home:/rmax:/fossil/">
here.</a>
Cryptographic checksums for download files are
<a href="http://www.hwaci.com/fossil_download_checksums.html">here</a>.
</small></p>
<table cellpadding="10">
}

# Find all unique timestamps.
#
set in [open {|fossil uv list} rb]
while {[gets $in line]>0} {
  set fn [lindex $line 5]
  set filesize($fn) [lindex $line 3]
  if {[regexp -- {-(\d\.\d+)\.(tar\.gz|zip)$} $fn all version]} {
    set filehash($fn) [lindex $line 1]
    set avers($version) 1
  }
}
close $in

set vdate(2.0) 2017-03-03
set vdate(1.37) 2017-01-15

# Do all versions from newest to oldest
#
foreach vers [lsort -decr -real [array names avers]] {
  #  set hr "../timeline?c=version-$vers;y=ci"
  set v2 v[string map {. _} $vers]
  set hr "../doc/trunk/www/changes.wiki#$v2"
  puts $out "<tr><td colspan=6 align=left><hr>"
  puts $out "<center><b><a href=\"$hr\">Version $vers</a>"
  if {[info exists vdate($vers)]} {
    set hr2 "../timeline?c=version-$vers&amp;y=ci"
    puts $out " (<a href='$hr2'>$vdate($vers)</a>)"
  }
  puts $out "</b></center>"
  puts $out "</td></tr>"
  puts $out "<tr>"

  foreach {prefix img desc} {
    fossil-linux linux.gif {Linux 3.x x64}
    fossil-macosx mac.gif {Mac 10.x x86}
    fossil-openbsd-x86 openbsd.gif {OpenBSD 5.x x86}
    fossil-w32 win32.gif {Windows}
    fossil-src src.gif {Source Tarball}
  } {
    set glob download/$prefix*-$vers*
    set filename [array names filesize $glob]
    if {[info exists filesize($filename)]} {
      set size [set filesize($filename)]
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
#
#  if {[info exists filesize(download/releasenotes-$vers.html)]} {
#    puts $out "<tr><td colspan=6 align=left>"
#    set rn [|open uv cat download/releasenotes-$vers.html]
#    fconfigure $rn -encoding utf-8
#    puts $out "[read $rn]"
#    close $rn
#    puts $out "</td></tr>"
#  }
}
puts $out "<tr><td colspan=5><hr></td></tr>"

puts $out {</table></center></div>}
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
<a href="/uv/download.html">Fossil website</a>.</p>
<pre>}

foreach {line} [split [exec fossil sql "SELECT hash, name FROM unversioned\
                                     WHERE name GLOB '*.tar.gz' OR\
                                           name GLOB '*.zip'"] \n] {
  set x [split $line |]
  set hash [lindex $x 0]
  set nm [file tail [lindex $x 1]]
  puts $out "$hash   $nm"
}
puts $out {</pre></body></html>}
close $out
