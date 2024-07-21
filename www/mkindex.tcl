#!/usr/bin/env tclsh
#
# Run this TCL script to generate a WIKI page that contains a
# permuted index of the various documentation files.
#
#    tclsh mkindex.tcl
#
# 2021-02-26:  The permuted index feature has been removed because
# moderns don't understand such things, and seeing so many entries
# confuses them.
#

set doclist {
  aboutcgi.wiki {How CGI Works In Fossil}
  aboutdownload.wiki {How The Download Page Works}
  adding_code.wiki {Adding New Features To Fossil}
  adding_code.wiki {Hacking Fossil}
  alerts.md {Email Alerts And Notifications}
  antibot.wiki {Defense against Spiders and Robots}
  backoffice.md {The "Backoffice" mechanism of Fossil}
  backup.md {Backing Up a Remote Fossil Repository}
  blame.wiki {The Annotate/Blame Algorithm Of Fossil}
  blockchain.md {Is Fossil A Blockchain?}
  branching.wiki {Branching, Forking, Merging, and Tagging}
  bugtheory.wiki {Bug Tracking In Fossil}
  build.wiki {Compiling and Installing Fossil}
  cap-theorem.md {Fossil and the CAP Theorem}
  caps/ {Administering User Capabilities (a.k.a. Permissions)}
  caps/admin-v-setup.md {Differences Between Setup and Admin Users}
  caps/ref.html {User Capability Reference}
  cgi.wiki {CGI Script Configuration Options}
  changes.wiki {Fossil Changelog}
  chat.md {Fossil Chat}
  checkin_names.wiki {Check-in And Version Names}
  checkin.wiki {Check-in Checklist}
  childprojects.wiki {Child Projects}
  chroot.md {Server Chroot Jail}
  ckout-workflows.md {Check-Out Workflows}
  co-vs-up.md {Checkout vs Update}
  colordiff.md {Colorized Diffs}
  copyright-release.html {Contributor License Agreement}
  concepts.wiki {Fossil Core Concepts}
  contact.md {Developer Contact Information}
  containers.md {OCI Containers}
  contribute.wiki {Contributing Code or Documentation To The Fossil Project}
  css-tricks.md {Fossil CSS Tips and Tricks}
  customgraph.md {Theming: Customizing the Timeline Graph}
  customskin.md {Theming: Customizing The Appearance of Web Pages}
  customskin.md {Custom Skins}
  custom_ticket.wiki {Customizing The Ticket System}
  defcsp.md {The Default Content Security Policy}
  delta-manifests.md {Delta Manifests}
  delta_encoder_algorithm.wiki {Fossil Delta Encoding Algorithm}
  delta_format.wiki {Fossil Delta Format}
  embeddeddoc.wiki {Embedded Project Documentation}
  encryptedrepos.wiki {How To Use Encrypted Repositories}
  env-opts.md {Environment Variables and Global Options}
  event.wiki {Events}
  faq.wiki {Frequently Asked Questions}
  fileedit-page.md {The fileedit Page}
  fileformat.wiki {Fossil File Format}
  fiveminutes.wiki {Up and Running in 5 Minutes as a Single User}
  forum.wiki {Fossil Forums}
  foss-cklist.wiki {Checklist For Successful Open-Source Projects}
  fossil-from-msvc.wiki {Integrating Fossil in the Microsoft Express 2010 IDE}
  fossil-is-not-relational.md {Introduction to the (Non-relational) Fossil Data Model}
  fossil_prompt.wiki {Fossilized Bash Prompt}
  fossil-v-git.wiki {Fossil Versus Git}
  gitusers.md {Git to Fossil Translation Guide}
  globs.md {File Name Glob Patterns}
  glossary.md {Glossary}
  grep.md {Fossil grep vs POSIX grep}
  hacker-howto.wiki {Hacker How-To}
  hacker-howto.wiki {Fossil Developers Guide}
  hashes.md {Hashes: Fossil Artifact Identification}
  hashpolicy.wiki {Hash Policy: Choosing Between SHA1 and SHA3-256}
  /help {Lists of Commands and Webpages}
  hints.wiki {Fossil Tips And Usage Hints}
  history.md {The Purpose And History Of Fossil}
  index.wiki {Home Page}
  inout.wiki {Import And Export To And From Git}
  interwiki.md {Interwiki Links}
  image-format-vs-repo-size.md {Image Format vs Fossil Repo Size}
  javascript.md {Use of JavaScript in Fossil}
  json-api/index.md {JSON API}
  loadmgmt.md {Managing Server Load}
  makefile.wiki {The Fossil Build Process}
  mirrorlimitations.md {Limitations On Git Mirrors}
  mirrortogithub.md {How To Mirror A Fossil Repository On GitHub}
  /md_rules {Markdown Formatting Rules}
  newrepo.wiki {How To Create A New Fossil Repository}
  patchcmd.md {The "fossil patch" Command}
  password.wiki {Password Management And Authentication}
  pikchr.md {The Pikchr Diagram Language}
  pop.wiki {Principles Of Operation}
  private.wiki {Creating, Syncing, and Deleting Private Branches}
  qandc.wiki {Questions And Criticisms}
  quickstart.wiki {Fossil Quick Start Guide}
  quotes.wiki
      {Quotes: What People Are Saying About Fossil, Git, and DVCSes in General}
  ../test/release-checklist.wiki {Pre-Release Testing Checklist}
  rebaseharm.md {Rebase Considered Harmful}
  reviews.wiki {Reviews}
  selfcheck.wiki {Fossil Repository Integrity Self Checks}
  selfhost.wiki {Fossil Self Hosting Repositories}
  server/ {How To Configure A Fossil Server}
  serverext.wiki {CGI Server Extensions}
  serverext.wiki {Adding Extensions To A Fossil Server Using CGI Scripts}
  settings.wiki {Fossil Settings}
  /sitemap {Site Map}
  shunning.wiki {Shunning: Deleting Content From Fossil}
  stats.wiki {Performance Statistics}
  style.wiki {Source Code Style Guidelines}
  ssl.wiki {Using SSL with Fossil}
  ssl-server.md {SSL/TLS Server Mode}
  sync.wiki {The Fossil Sync Protocol}
  tech_overview.wiki {A Technical Overview Of The Design And Implementation
                      Of Fossil}
  tech_overview.wiki {SQLite Databases Used By Fossil}
  th1.md {The TH1 Scripting Language}
  theory1.wiki {Thoughts On The Design Of The Fossil DVCS}
  tickets.wiki {The Fossil Ticket System}
  unvers.wiki {Unversioned Files}
  webpage-ex.md {Webpage Examples}
  webui.wiki {The Fossil Web Interface}
  whyallinone.md {Why Bundle Forum, Wiki, and other Web Software With Your DVCS?}
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

# Disable the permutations.
#  for {set i 0} {$i<$n-1} {incr i} {
#    set prefix [lrange $title 0 $i]
#    set suffix [lrange $title [expr {$i+1}] end]
#    set firstword [string tolower [lindex $suffix 0]]
#    if {[lsearch $stopwords $firstword]<0} {
#      lappend permindex [list "$suffix &mdash; $prefix" $file 0]
#    }
#  }
}
set permindex [lsort -dict -index 0 $permindex]
set out [open permutedindex.html w]
fconfigure $out -encoding utf-8 -translation lf
puts $out \
"<div class='fossil-doc' data-title='Index Of Fossil Documentation'>"
puts $out {
<form action='$ROOT/docsrch' method='GET' style="text-align:center">
<input type="text" name="s" size="40" autofocus>
<input type="submit" value="Search Docs">
</form>
<h2>Primary Documents:</h2>
<ul>
<li> <a href='quickstart.wiki'>Quick-start Guide</a>
<li> <a href='$ROOT/help'>Built-in help for commands and webpages</a>
<li> <a href='history.md'>Purpose and History of Fossil</a>
<li> <a href='build.wiki'>Compiling and installing Fossil</a>
<li> <a href='../COPYRIGHT-BSD2.txt'>License</a>
<li> <a href='userlinks.wiki'>Miscellaneous Docs for Fossil Users</a>
<li> <a href='hacker-howto.wiki'>Fossil Developer's Guide</a>
<li><a href='$ROOT/wiki?name=Release Build How-To'>Release Build How-To</a>,
a.k.a.  how deliverables are built</li>
</li>
<li> <a href='$ROOT/wiki?name=To+Do+List'>To Do List (Wiki)</a>
<li> <a href='https://fossil-scm.org/fossil-book/'>Fossil book</a>
</ul>
<h2 id="pindex">Other Documents:</h2>
<ul>}
foreach entry $permindex {
  foreach {title file bold} $entry break
#  if {$bold} {set title <b>$title</b>}
  if {[string match /* $file]} {set file ../../..$file}
  puts $out "<li><a href=\"$file\">$title</a></li>"
}
puts $out "</ul></div>"
