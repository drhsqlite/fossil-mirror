#!/usr/bin/tclsh
#
# Run this script to build the "download" page on standard output.
#
#
puts \
{<html>
<head>
<title>Fossil Download</title>
</head>
<body>
<h1>Fossil Download</h1>

<p>
This page contains prebuilt binaries for 
<a href="index.html">fossil</a> for various architectures.
The source code is available in the
<a href="http://www.fossil-scm.org/fossil/timeline">self-hosting
fossil repository</a>.
</p>

<table cellpadding="5">
}

proc Product {pattern desc} {
  set flist [glob -nocomplain download/$pattern]
  foreach file [lsort -dict $flist] {
    set file [file tail $file]
    if {![regexp -- {-([a-f0-9]{10})[^a-f0-9]} $file all version]} continue
    set mtime [file mtime download/$file]
    set date [clock format $mtime -format {%Y-%m-%d %H:%M:%S UTC} -gmt 1]
    set size [file size download/$file]
    set units bytes
    if {$size>1024*1024} {
      set size [format %.2f [expr {$size/(1024.0*1024.0)}]]
      set units MiB
    } elseif {$size>1024} {
      set size [format %.2f [expr {$size/(1024.0)}]]
      set units KiB
    }
    puts "<tr><td width=\"10\"></td>"
    puts "<td valign=\"top\" align=\"right\">"
    puts "<a href=\"download/$file\">$file</a></td>"
    puts "<td width=\"5\"></td>"
    regsub -all VERSION $desc $version d2
    puts "<td valign=\"top\">[string trim $d2].<br>Size: $size $units.<br>"
    puts "Created: $date</td></tr>"
  }
}

Product fossil-linux-x86-*.gz {
  Prebuilt fossil binary version [VERSION] for Linux on x86
}
Product fossil-macosx-x86-*.gz {
  Prebuilt fossil binary version [VERSION] for MacOSX on x86
}
Product fossil-w32-*.zip {
  Prebuilt fossil binary version [VERSION] for windows
}


puts {</table>
</body>
</html>
}
