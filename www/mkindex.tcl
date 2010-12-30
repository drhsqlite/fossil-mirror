#!/bin/sh
#
# Run this TCL script to generate a WIKI page that contains a 
# permuted index of the various documentation files.
#
#    tclsh mkindex.tcl >permutedindex.wiki
#

set doclist {
  bugtheory.wiki {Bug Tracking In Fossil}
  branching.wiki {Branching, Forking, Merging, and Tagging}
  build.wiki {Building and Installing Fossil}
  checkin_names.wiki {Checkin And Version Names}
  concepts.wiki {Fossil Core Concepts}
  custom_ticket.wiki {Customizing The Ticket System}
  delta_encoder_algorithm.wiki {Fossil Delta Encoding Algorithm}
  delta_format.wiki {Fossil Delta Format}
  embeddeddoc.wiki {Embedded Project Documentation}
  event.wiki {Events}
  faq.wiki {Frequently Asked Questions}
  fileformat.wiki {Fossil File Format}
  fossil-v-git.wiki {Fossil Versus Git}
  index.wiki {Home Page}
  inout.wiki {Import And Export To And From Git}
  makefile.wiki {The Fossil Build Process}
  password.wiki {Password Management And Authentication}
  pop.wiki {Principles Of Operations}
  qandc.wiki {Questions And Criticisms}
  quickstart.wiki {Fossil Quick Start Guide}
  quotes.wiki
      {Quotes: What People Are Saying About Fossil, Git, and DVCSes in General}
  selfcheck.wiki {Fossil Repository Integrity Self Checks}
  selfhost.wiki {Fossil Self Hosting Repositories}
  server.wiki {How To Configure A Fossil Server}
  shunning.wiki {Deleting Content From Fossil}
  stats.wiki {Performance Statistics}
  sync.wiki {The Fossil Sync Protocol}
  theory1.wiki {Thoughts On The Design Of The Fossil DVCS}
  webui.wiki {The Fossil Web Interface}
  wikitheory.wiki {Wiki In Fossil}
}

set permindex {}
set stopwords {fossil and a in of on the to are about}
foreach {file title} $doclist {
  set n [llength $title]
  lappend permindex [list $title $file]
  for {set i 0} {$i<$n-1} {incr i} {
    set prefix [lrange $title 0 $i]
    set suffix [lrange $title [expr {$i+1}] end]
    set firstword [string tolower [lindex $suffix 0]]
    if {[lsearch $stopwords $firstword]<0} {
      lappend permindex [list "$suffix &#151; $prefix" $file]
    }
  }
}
set permindex [lsort -dict $permindex]
puts "<title>Permuted Index Of Fossil Documentation</title>"
puts "<nowiki>"
puts "<ul>"
foreach entry $permindex {
  foreach {title file} $entry break
  puts "<li><a href=\"$file\">$title</a></li>"
}
puts "</ul>"
