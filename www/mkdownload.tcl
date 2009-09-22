#!/usr/bin/tclsh
#
# Run this script to build the "download" page on standard output.
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
<div class="mainmenu"><a href='/fossil/doc/tip/www/index.wiki'>Home</a><a href='/fossil/leaves'>Leaves</a><a href='/fossil/timeline'>Timeline</a><a href='/fossil/brlist'>Branches</a><a href='/fossil/taglist'>Tags</a><a href='/fossil/reportlist'>Tickets</a><a href='/fossil/wiki'>Wiki</a><a href='/fossil/login'>Login</a></div>
<div class="content">
<p>

<p>
Click on links below to download prebuilt binaries and source tarballs for 
recent versions of <a href="/fossil">Fossil</a>.
The historical source code is also available in the
<a href="/fossil/doc/tip/www/selfhost.wiki">self-hosting
Fossil repositories</a>.
</p>

<table cellpadding="5">
}

proc Product {pattern desc} {
  set flist [glob -nocomplain download/$pattern]
  foreach file [lsort -dict $flist] {
    set file [file tail $file]
    if {![regexp -- {-([0-9]+)\.} $file all version]} continue
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

Product fossil-linux-x86-*.zip {
  Prebuilt fossil binary version [VERSION] for Linux on x86
}
Product fossil-linux-amd64-*.zip {
  Prebuilt fossil binary version [VERSION] for Linux on amd64
}
Product fossil-macosx-x86-*.zip {
  Prebuilt fossil binary version [VERSION] for MacOSX on x86
}
Product fossil-w32-*.zip {
  Prebuilt fossil binary version [VERSION] for windows
}
Product fossil-src-*.tar.gz {
  Source code tarball for fossil version [VERSION]
}

puts {</table>
</body>
</html>
}
