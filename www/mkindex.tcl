#!/bin/sh
#
# Run this TCL script to generate a WIKI page that contains a
# permuted index of the various documentation files.
#
#    tclsh mkindex.tcl >permutedindex.html
#

set doclist {
  adding_code.wiki {Adding New Features To Fossil}
  adding_code.wiki {Hacking Fossil}
  antibot.wiki {Defense against Spiders and Bots}
  bugtheory.wiki {Bug Tracking In Fossil}
  branching.wiki {Branching, Forking, Merging, and Tagging}
  build.wiki {Compiling and Installing Fossil}
  checkin_names.wiki {Checkin And Version Names}
  checkin.wiki {Check-in Checklist}
  changes.wiki {Fossil Changelog}
  copyright-release.html {Contributor License Agreement}
  concepts.wiki {Fossil Core Concepts}
  contribute.wiki {Contributing Code or Documentation To The Fossil Project}
  customskin.md {Theming: Customizing The Appearance of Web Pages}
  custom_ticket.wiki {Customizing The Ticket System}
  delta_encoder_algorithm.wiki {Fossil Delta Encoding Algorithm}
  delta_format.wiki {Fossil Delta Format}
  embeddeddoc.wiki {Embedded Project Documentation}
  event.wiki {Events}
  faq.wiki {Frequently Asked Questions}
  fileformat.wiki {Fossil File Format}
  fiveminutes.wiki {Update and Running in 5 Minutes as a Single User}
  foss-cklist.wiki {Checklist For Successful Open-Source Projects}
  fossil-from-msvc.wiki {Integrating Fossil in the Microsoft Express 2010 IDE}
  fossil-v-git.wiki {Fossil Versus Git}
  hacker-howto.wiki {Hacker How-To}
  hints.wiki {Fossil Tips And Usage Hints}
  index.wiki {Home Page}
  inout.wiki {Import And Export To And From Git}
  makefile.wiki {The Fossil Build Process}
  newrepo.wiki {How To Create A New Fossil Repository}
  password.wiki {Password Management And Authentication}
  pop.wiki {Principles Of Operations}
  private.wiki {Creating, Syncing, and Deleting Private Branches}
  qandc.wiki {Questions And Criticisms}
  quickstart.wiki {Fossil Quick Start Guide}
  quotes.wiki
      {Quotes: What People Are Saying About Fossil, Git, and DVCSes in General}
  ../test/release-checklist.wiki {Pre-Release Testing Checklist}
  reviews.wiki {Reviews}
  selfcheck.wiki {Fossil Repository Integrity Self Checks}
  selfhost.wiki {Fossil Self Hosting Repositories}
  server.wiki {How To Configure A Fossil Server}
  settings.wiki {Fossil Settings}
  shunning.wiki {Shunning: Deleting Content From Fossil}
  stats.wiki {Performance Statistics}
  style.wiki {Source Code Style Guidelines}
  ssl.wiki {Using SSL with Fossil}
  sync.wiki {The Fossil Sync Protocol}
  tech_overview.wiki {A Technical Overview Of The Design And Implementation
                      Of Fossil}
  tech_overview.wiki {SQLite Databases Used By Fossil}
  th1.md {The TH1 Scripting Language}
  tickets.wiki {The Fossil Ticket System}
  theory1.wiki {Thoughts On The Design Of The Fossil DVCS}
  webui.wiki {The Fossil Web Interface}
  wikitheory.wiki {Wiki In Fossil}
}

set permindex {}
set stopwords {fossil and a in of on the to are about used by for or}
foreach {file title} $doclist {
  set n [llength $title]
  regsub -all {\s+} $title { } title
  lappend permindex [list $title $file]
  for {set i 0} {$i<$n-1} {incr i} {
    set prefix [lrange $title 0 $i]
    set suffix [lrange $title [expr {$i+1}] end]
    set firstword [string tolower [lindex $suffix 0]]
    if {[lsearch $stopwords $firstword]<0} {
      lappend permindex [list "$suffix &mdash; $prefix" $file]
    }
  }
}
set permindex [lsort -dict -index 0 $permindex]
set out [open permutedindex.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out \
"<div class='fossil-doc' data-title='Index Of Fossil Documentation'>"
puts $out {
<center>
<form action='../../../docsrch' method='GET'>
<input type="text" name="s" size="40" autofocus>
<input type="submit" value="Search Docs">
</form>
</center>
<h2>Primary Documents:</h2>
<ul>
<li> <a href='quickstart.wiki'>Quick-start Guide</a>
<li> <a href='faq.wiki'>FAQ</a>
<li> <a href='build.wiki'>Compiling and installing Fossil</a>
<li> <a href='../COPYRIGHT-BSD2.txt'>License</a>
<li> <a href='http://www.fossil-scm.org/schimpf-book/home'>Jim Schimpf's
book</a>
<li> <a href='../../../help'>Command-line help</a>
</ul>
<a name="pindex"></a>
<h2>Permuted Index:</h2>
<ul>}
foreach entry $permindex {
  foreach {title file} $entry break
  puts $out "<li><a href=\"$file\">$title</a></li>"
}
puts $out "</ul></div>"
