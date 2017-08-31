#!/usr/bin/tclsh
#
# Run this to generate the FAQ
#
set cnt 1
proc faq {question answer} {
  set ::faq($::cnt) [list [string trim $question] [string trim $answer]]
  incr ::cnt
}

faq {
  What GUIs are available for fossil?
} {
  The fossil executable comes with a [./webui.wiki | web-based GUI] built in.
  Just run:

  <blockquote>
  <b>fossil [/help/ui|ui]</b> <i>REPOSITORY-FILENAME</i>
  </blockquote>

  And your default web browser should pop up and automatically point to
  the fossil interface.  (Hint:  You can omit the <i>REPOSITORY-FILENAME</i>
  if you are within an open check-out.)
}

faq {
  What is the difference between a "branch" and a "fork"?
} {
  This is a big question - too big to answer in a FAQ.  Please
  read the <a href="branching.wiki">Branching, Forking, Merging,
  and Tagging</a> document.
}


faq {
  How do I create a new branch?
} {
  There are lots of ways:

  When you are checking in a new change using the <b>[/help/commit|commit]</b>
  command, you can add the option  "--branch <i>BRANCH-NAME</i>" to
  make the new check-in be the first check-in for a new branch.

  If you want to create a new branch whose initial content is the
  same as an existing check-in, use this command:

  <blockquote>
  <b>fossil [/help/branch|branch] new</b> <i>BRANCH-NAME BASIS</i>
  </blockquote>

  The <i>BRANCH-NAME</i> argument is the name of the new branch and the
  <i>BASIS</i> argument is the name of the check-in that the branch splits
  off from.

  If you already have a fork in your check-in tree and you want to convert
  that fork to a branch, you can do this from the web interface.
  First locate the check-in that you want to be
  the initial check-in of your branch on the timeline and click on its
  link so that you are on the <b>ci</b> page.  Then find the "<b>edit</b>"
  link (near the "Commands:" label) and click on that.  On the
  "Edit Check-in" page, check the box beside "Branching:" and fill in
  the name of your new branch to the right and press the "Apply Changes"
  button.
}

faq {
  How do I tag a check-in?
} {
  There are several ways:

  When you are checking in a new change using the <b>[/help/commit|commit]</b>
  command, you can add a tag to that check-in using the
  "--tag <i>TAGNAME</i>" command-line option.  You can repeat the --tag
  option to give a check-in multiple tags.  Tags need not be unique.  So,
  for example, it is common to give every released version a "release" tag.

  If you want add a tag to an existing check-in, you can use the
  <b>[/help/tag|tag]</b> command.  For example:

  <blockquote>
  <b>fossil [/help/branch|tag] add</b> <i>TAGNAME</i> <i>CHECK-IN</i>
  </blockquote>

  The CHECK-IN in the previous line can be any
  [./checkin_names.wiki | valid check-in name format].

  You can also add (and remove) tags from a check-in using the
  [./webui.wiki | web interface].  First locate the check-in that you
  what to tag on the timeline, then click on the link to go the detailed
  information page for that check-in.  Then find the "<b>edit</b>"
  link (near the "Commands:" label) and click on that.  There are
  controls on the edit page that allow new tags to be added and existing
  tags to be removed.
}

faq {
  How do I create a private branch that won't get pushed back to the
  main repository.
} {
  Use the <b>--private</b> command-line option on the
  <b>commit</b> command.  The result will be a check-in which exists on
  your local repository only and is never pushed to other repositories.
  All descendants of a private check-in are also private.

  Unless you specify something different using the <b>--branch</b> and/or
  <b>--bgcolor</b> options, the new private check-in will be put on a branch
  named "private" with an orange background color.

  You can merge from the trunk into your private branch in order to keep
  your private branch in sync with the latest changes on the trunk.  Once
  you have everything in your private branch the way you want it, you can
  then merge your private branch back into the trunk and push.  Only the
  final merge operation will appear in other repositories.  It will seem
  as if all the changes that occurred on your private branch occurred in
  a single check-in.
  Of course, you can also keep your branch private forever simply
  by not merging the changes in the private branch back into the trunk.

  [./private.wiki | Additional information]
}

faq {
  How can I delete inappropriate content from my fossil repository?
} {
  See the article on [./shunning.wiki | "shunning"] for details.
}

faq {
  How do I make a clone of the fossil self-hosting repository?
} {
  Any of the following commands should work:
  <blockquote><pre>
  fossil [/help/clone|clone]  http://www.fossil-scm.org/  fossil.fossil
  fossil [/help/clone|clone]  http://www2.fossil-scm.org/  fossil.fossil
  fossil [/help/clone|clone]  http://www3.fossil-scm.org/site.cgi  fossil.fossil
  </pre></blockquote>
  Once you have the repository cloned, you can open a local check-out
  as follows:
  <blockquote><pre>
  mkdir src; cd src; fossil [/help/open|open] ../fossil.fossil
  </pre></blockquote>
  Thereafter you should be able to keep your local check-out up to date
  with the latest code in the public repository by typing:
  <blockquote><pre>
  fossil [/help/update|update]
  </pre></blockquote>
}

faq {
  How do I import or export content from and to other version control systems?
} {
  Please see [./inout.wiki | Import And Export]
}



#############################################################################
# Code to actually generate the FAQ
#
puts "<title>Fossil FAQ</title>"
puts "<h1 align=\"center\">Frequently Asked Questions</h1>\n"
puts "<p>Note: See also <a href=\"qandc.wiki\">Questions and Criticisms</a>.\n"

puts {<ol>}
for {set i 1} {$i<$cnt} {incr i} {
  puts "<li><a href=\"#q$i\">[lindex $faq($i) 0]</a></li>"
}
puts {</ol>}
puts {<hr>}

for {set i 1} {$i<$cnt} {incr i} {
  puts "<a name=\"q$i\"></a>"
  puts "<p><b>($i) [lindex $faq($i) 0]</b></p>\n"
  set body [lindex $faq($i) 1]
  regsub -all "\n *" [string trim $body] "\n" body
  puts "<blockquote>$body</blockquote></li>\n"
}
puts {</ol>}
