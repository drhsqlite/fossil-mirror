#!/usr/bin/env tclsh
#
# Run this TCL script to generate a WIKI page that contains a
# permuted index of the various documentation files.
#
#    tclsh mkindex.tcl
#

set doclist {
  aboutcgi.wiki {How CGI Works In Fossil}
  aboutdownload.wiki {How The Download Page Works}
  adding_code.wiki {Adding New Features To Fossil}
  adding_code.wiki {Hacking Fossil}
  antibot.wiki {Defense against Spiders and Bots}
  blame.wiki {The Annotate/Blame Algorithm Of Fossil}
  branching.wiki {Branching, Forking, Merging, and Tagging}
  bugtheory.wiki {Bug Tracking In Fossil}
  build.wiki {Compiling and Installing Fossil}
  changes.wiki {Fossil Changelog}
  checkin_names.wiki {Check-in And Version Names}
  checkin.wiki {Check-in Checklist}
  childprojects.wiki {Child Projects}
  copyright-release.html {Contributor License Agreement}
  concepts.wiki {Fossil Core Concepts}
  contribute.wiki {Contributing Code or Documentation To The Fossil Project}
  customgraph.md {Theming: Customizing the Timeline Graph}
  customskin.md {Theming: Customizing The Appearance of Web Pages}
  custom_ticket.wiki {Customizing The Ticket System}
  delta_encoder_algorithm.wiki {Fossil Delta Encoding Algorithm}
  delta_format.wiki {Fossil Delta Format}
  embeddeddoc.wiki {Embedded Project Documentation}
  encryptedrepos.wiki {How To Use Encrypted Repositories}
  env-opts.md {Environment Variables and Global Options}
  event.wiki {Events}
  faq.wiki {Frequently Asked Questions}
  fileformat.wiki {Fossil File Format}
  fiveminutes.wiki {Up and Running in 5 Minutes as a Single User}
  foss-cklist.wiki {Checklist For Successful Open-Source Projects}
  fossil-from-msvc.wiki {Integrating Fossil in the Microsoft Express 2010 IDE}
  fossil-v-git.wiki {Fossil Versus Git}
  globs.md {File Name Glob Patterns}
  hacker-howto.wiki {Hacker How-To}
  hashpolicy.wiki {Hash Policy: Choosing Between SHA1 and SHA3-256}
  /help {Lists of Commands and Webpages}
  hints.wiki {Fossil Tips And Usage Hints}
  index.wiki {Home Page}
  inout.wiki {Import And Export To And From Git}
  makefile.wiki {The Fossil Build Process}
  /md_rules {Markdown Formatting Rules}
  newrepo.wiki {How To Create A New Fossil Repository}
  password.wiki {Password Management And Authentication}
  pop.wiki {Principles Of Operation}
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
  /sitemap {Site Map}
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
  unvers.wiki {Unversioned Files}
  webpage-ex.md {Webpage Examples}
  webui.wiki {The Fossil Web Interface}
  whyusefossil.wiki {Why You Should Use Fossil}
  whyusefossil.wiki {Benefits Of Version Control}
  wikitheory.wiki {Wiki In Fossil}
  /wiki_rules {Wiki Formatting Rules}
}

set permindex {}
set stopwords {
   a about against and are as by for fossil from in of on or should the to use
   used with
}
foreach {file title} $doclist {
  set n [llength $title]
  regsub -all {\s+} $title { } title
  lappend permindex [list $title $file 1]
  for {set i 0} {$i<$n-1} {incr i} {
    set prefix [lrange $title 0 $i]
    set suffix [lrange $title [expr {$i+1}] end]
    set firstword [string tolower [lindex $suffix 0]]
    if {[lsearch $stopwords $firstword]<0} {
      lappend permindex [list "$suffix &mdash; $prefix" $file 0]
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
<form action='$ROOT/docsrch' method='GET'>
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
<li> <a href='$ROOT/help'>List of commands, web-pages, and settings</a>
</ul>
<a name="pindex"></a>
<h2>Permuted Index:</h2>
<ul>}
foreach entry $permindex {
  foreach {title file bold} $entry break
  if {$bold} {set title <b>$title</b>}
  if {[string match /* $file]} {set file ../../..$file}
  puts $out "<li><a href=\"$file\">$title</a></li>"
}
puts $out "</ul></div>"
