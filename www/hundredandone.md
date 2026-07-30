# 101 Reasons Why Fossil Is Better Than Git

*This is a work in progress.  Only 60 reasons have been typed in so far,
but I have a separate text file of notes that lists 104 candidate reasons.
It's just taking me a while to compose and edit the rationale for each
one, and to arrange the reasons in a sensible order.  I will merge this
document from its current branch onto trunk when it gets closer to being
ready to publish.*

  1.  **Fossil comes as a single self-contained executable file**.<p>
      Install Fossil by copying "fossil" (or "fossil.exe") to someplace
      on your $PATH (or %PATH%).  Upgrade (or downgrade) by overwriting
      that one file.  Uninstall by deleting that one file.<p>
      A Git installation requires hundreds of files.  You want to use
      package manager to install, upgrade, or uninstall Git.  If you
      tinker with individual files of a Git installation, you run a high
      risk of messing things up.

  1.  **Fossil comes with a built-in full-featured web interface**.<p>
      The Fossil web interface is on par with GitHub, GitLab,
      Gitea, and similar.  Some people think of Fossil as
      "Github-in-a-box".  Git has "gitweb", but that CGI program
      is so limited and difficult to set up and use that few people
      even know it exists, so I do not count it as a built-in web
      interface for the purposes of this point.

  1.  **You can run the Fossil web interface locally
      using the "fossil ui" command.**<p>
      Just type the command "fossil ui" from any Fossil checkout,
      or add the name of a Fossil repository file as an argument,
      and Fossil automatically brings up the full web-based UI
      in your preferred web browser.  This works seamlessly on all
      platforms.  There is nothing extra to install.  There is
      no configuration or setup.  It just works.

  1.  **The Fossil web interface shows a graphical timeline of changes.**<p>
      See, for example <https://sqlite.org/src/timeline> or
      <https://fossil-scm.org/home/timeline>.  Similar timeline features
      for Git are available from the command-line for a local clone
      (for example using "gitk" or other third-party programs), but
      nothing with anything close to the capabilities of the Fossil
      timeline is available via a web interface, as far as I know.<p>
      Using the web interface, one can easily check on the status
      of a project when away from the office and without access to a clone
      of the repository, just by visiting a link using
      any web browser, even from a phone.

  1.  **The graphical timeline dynamically adjusts its layout as you resize 
      your browser window.**<p>
      The server sends down JSON that gives the basic structure of the
      timeline graph, then Javascript renders the graph.  The JS code is
      small, does not use any third-party frameworks, and is (by default)
      appended to the HTML page.  There is a separate CSS file, but apart
      from that, the HTML page is completely stand-alone.  The local
      web browser isn't required to go gather lots of separate resources.

  1.  **The graphical timeline works on a phone.**<p>
      The layout can get a little cramped on a small display.  The
      timeline does look better on a desktop.
      Even so, it is functional on a phone and it is very convenient
      to be able to see what is happening on a project
      while away from the office and without access to a laptop.

  1.  **The graphical timeline is bandwidth efficient.**<p>
      To display a timeline of recent activity on Fossil uses less
      than 5% of the bandwidth as does GitHub.  In a typical example,
      GitHub requires about 3.5MB of transfer compared to 150KB for
      Fossil.  And for all that 3.5MB, GitHub just gives you a list
      of recent check-ins without any indication of the branching
      structure, whereas Fossil gives you an easy-to-read color-coded
      graph.

  1.  **The Fossil web interface makes it easy to see a diff between
      any two checkins with just a couple of clicks.**<p>
      On the timeline display, click on one node of the graph to
      select it (a red dot will appear in the center of the node) and
      then click on any other graph node, and Fossil will compute and
      display a diff between those two check-ins.

  1.  **The Fossil web-based diff page shows the context of the two
      checkins being diffed.**<p>
      At the top of the web-based diff is a graph that shows
      specifically the two checkins being diffed and the context
      around them.
      [Example](/vdiff?from=052390edaf04a5f5&to=9b686daeeda7b4ca).
      This helps to relieve any confusion about what you are looking at.

  1.  **In Fossil, a repository is distinct from a working check-out.**<p>
      A Fossil repository can be colocated with the working check-out, as
      they are required to be in Git.  But most people keep the repository
      separate.  One common pattern is to put all Fossil repositories in
      a single directory named $HOME/Fossils or $HOME/Museum, and then open 
      working check-outs against each repository whereever they are needed.

  1.  **A single Fossil repository can support multiple working check-outs.**
      <p>
      Git has worktrees, but all worktrees checkouts must be on separate
      branches.  Fossil allows multiple working check-outs on the same branch
      or even on the same checkin.  One common pattern is to have one
      check-out that is being edited, and another than is a pristine, unedited
      version of the same checkin.  This allows both to be compiled
      simultanteously for performance comparison, or to step through both
      binaries in two separate "gdb" sessions to hunt down a bug.

  1.  **A Fossil repository is a single disk file, not a directory
      hierarchy.**<p>
      You can "mv" a Fossil repository to a new place.  You can "scp"
      a Fossil repository to another machine.  It is just a file,
      specifically an SQLite database file.

  1.  **Fossil does not have a staging area**.</p>
      A staging area adds no new capabilities.  (Partial commits
      are accomplished in Fossil simply by listing the subset of files to
      be committed on the "fossil commit" command line.)  A staging area
      only adds complication.  The staging area adds to the mental model 
      of the project that Git users need to keep up with, forcing the
      developer to spend more time thinking about the version control
      system and hence less time thinking about the project they are
      working on.

  1.  **Fossil remembers where all your repositories and working
      check-outs are located.**<p>
      This and other information (such as all your global settings)
      is stored in a per-user database file
      at $HOME/.config/fossil.db on unix or in %LOCALAPPDATA%/_fossil on
      Windows.  Fossil creates and manages that file automatically.
      The user never has know the file even exists.  If you
      move or rename repositories or check-outs, the database will get
      temporarily out of sync with reality, but Fossil will automatically
      fix the database the next time you do anything with the file or
      check-out  that was moved or renamed.<p>
      You can get a list of repositories using the "fossil all ls" command,
      or a list of open check-outs using "fossil all ls --ckout".

  1.  **Fossil enables you to bring up a web-based UI that shows all of
      your repositories at once.**<p>
      Run the command "fossil ui /" and your default web browser will pop
      of a screen that lists all of your repositories together with the
      associated project name and how recently that repository was modified.
      Click on links to bring up repository-specific web-pages.  Or click
      on column headers to sort by that column.<p>
      If you are like me and have hundreds of repositories on your desktop
      system, this feature makes it easier to keep track of them all.
      Are you on an infrequently used travel laptop and forgot where you
      put a particular repository, this feature helps you find it.

  1.  **Fossil allows multiple checkins to have the same tag.**<p>
      For example, on the SQLite project, every release is tagged with
      "release".

  1.  **The Fossil timeline can show all checkins with a specific tag.**<p>
      For example, to see all SQLite releases visit
      <https://sqlite.org/src/timeline?t=release> or to see all
      Fossil releases go to
      <https://fossil-scm.org/home/timeline?t=release>.

  1.  **Fossil has a built-in wiki**.<p>
      Wiki pages are colocated in the same repository as your code, so that
      they push, pull, sync, and clone together with your code.

  1.  **Fossil can associate a wiki page with a particular checkin.**<p>
      Rather than including a massive and verbose checkin comment on an
      important checkin (such as the merge of a big new feature), Fossil
      allows you to assign a wiki page to that checkin.  That wiki page
      is show as part of the checkin information in the web interface.
      See, for example, the "About" section of 
      [SQLite checkin 2019-11-21T18:28:44.463Z](https://sqlite.org/src/info/2019-11-21T18:28:44.463Z).
      The wiki page associated with a checkin can be created and revised
      after the checkin is comimtted and pushed.  So it can be used, for
      example, to add notes or bug reports that occur long after the
      checkin itself has been inserted into the DAG.

  1.  **Fossil can associate a wiki page with a particular branch.**<p>
      You can create a wiki page for a branch that documents the purpose
      of the branch.  The wiki page can be displayed separately (like any
      other wiki page) but is also displayed automatically at the top of
      every timeline for that branch.  See, for example,
      <https://sqlite.org/src/timeline?r=autosetup>.

  1.  **Fossil keeps track of historical branch names.**<p>
      Git does not actually keep track of branches.  Git gives a name
      to each leaf of the DAG and infers branches based on the name
      assigned to the leaf.  Fossil actually remembers the name of
      the branch.  For example, suppose a customer asks
      "Whatever became of that setlk-snapshot-fix branch you
      started last year?"  That branch still exists and you can
      [see it on GitHub](https://github.com/sqlite/sqlite/commits/setlk-snapshot-fix)
      and [in Fossil](https://sqlite.org/src/timeline?r=setlk-snapshot-fix).
      Notice, though, that GitHub does not show how the branch was resolved.
      (Aside: Also notice how much faster Fossil renders!)
      Fossil clearly shows that the branch ended up being merged into trunk.
      GitHub just shows us all ancestors of the leaf node labeled
      "setlk-snapshot-fix", including ancestors that were in other branches
      that got merged in, and ancestors that predate the founding of the
      setlk-snapshot-fix branch.
      <p>
      GitHub just cannot shows you the checkins of branch setlk-snapshot-fix
      only.  There are other third-party tools that will show you that, I am
      told, but they all require a local clone of the repository.  Apparently
      there is no way to see this information in a web browser running on
      your phone.

  1.  **Fossil allows multiple branches with the same name.**<p>
      This is used, for example, to name a lot for branches "experimental"
      or "mistake".  See
      <https://sqlite.org/src/timeline?r=experimental> and
      <https://sqlite.org/src/timeline?r=mistake>.  Or see
      [the empty-table-optimizations branch(es)](https://sqlite.org/src/timeline?r=empty-table-optimizations) which was initially merged to trunk and closed
      on 2025-07-02, but then reopened and continued with more enhancements
      until it was merged again on 2025-07-08.  

  1.  **Fossil allows legacy SHA1 hashes and newer SHA3-256 hashes in the
      same repository.**<p>
      Both Fossil and Git started out using SHA1 hashes.  But when the
      [SHAttered attack](https://www.marc-stevens.nl/research/shattered.io/)
      against SHA1 was published on 2017-02-23, the need to migrate to a
      stronger hash algorithm was recognized.  Fossil added the ability
      to use SHA3-256 as an alternative on 2017-03-01 (six days after the
      SHAttered attack was first published), and to this day
      it continues to support both.  SHA3-256 is now the default for all
      new repositories and checkins, though older checkins that occurred
      prior to ShAttered can still use their original SHA1 hash and so no
      repositories had to be rebuilt and no hyperlinks were broken.<p>
      In contrast, after nine years, a Git repository can still only 
      support only one hash algorithm at a time.
      Newer Git repositories are able to use SHA2, though the default
      is still SHA1.
 
  1.  **Fossil allows checkins to be identified by timestamp**<p>
      The canonical name for a checkin is its hash.  Both Git and Fossil
      allow a checkin to be identified by any unique prefix of its hash.
      But only Fossil allows a checkin to also be identified by its
      timestamp.  The names "2026-02-02T16:03:24.852Z" and "fdebbedbd9a99165"
      both refer to [the same checkin](https://sqlite.org/src/fdebbedbd9a99165)
      in SQLite, but the first one has the advantage of giving some time
      context rather than just being a seemingly random sequence of hexadecimal
      digits.  It is possible that two or more checkins can have the same
      timestamp, in which case the timestamp would be ambiguous.  And a
      checkin timestamp can be changed after it is committed, by using
      a special tag.  So timestamp identifiers do not have the uniqueness
      and stability guarantees as hash identifiers, but they are available
      as an option and are often useful.

  1.  **Fossil has a built-in forum.**<p>
      The forum content is replicated via push, pull, sync, and clone just like
      source code.  Forum posts can be enabled per user or for all user or
      for all users and anonymous passers-by.  Some user can be appointed
      as moderators and posts from untrusted users can be held for
      moderation.
      <p>
      The [Fossil Forum](https://fossil-scm.org/forum), the
      [SQLite User Forum](https://sqlite.org/forum), and the
      [SQLite Bugs Forum](https://sqlite.org/bugs) are all forums
      set up for specific purposes and deliberately kept separate
      from source code.  But other projects such as
      [Pikchr](https://pikchr.org/) comingle their forum and the source code
      in the same repository.  Thus when you clone the
      Pikchr source repository, you also get all the Forum history.

  1.  **Fossil makes it easy to set up a project website using CGI.**<p>
      If you have an internet-facing server running a CGI-capable web
      server, you can set up a set up a complete self-hosting project
      website with a simple CGI script.  The canonical 
      [Fossil website](https://fossil-scm.org/home) is really just a
      CGI script for the Fossil source repository.  When you clone the
      Fossil source code, you don't just get the code, you get the entire
      website.
      <p>
      The CGI script used to run the Fossil website looks like this:
      <pre>
         #!/usr/bin/fossil
         repository: /Fossils/fossil.fossil</pre>
      You can, of course, also create a self-hosting website using
      GitLab or Forgejo or similar, but the setup and maintenance is
      somewhat more involved.  You'll also probably need a bigger machine
      if you are using GitLab, whereas a Fossil website
      works fine on a 2GiB Raspberry PI or a $6/month VPS.  In fairness,
      I am told that Gitea and Forgejo also work well on a small machine.

  1.  **Fossil makes it easy to set up a project website using SCGI.**<p>
      CGI is easier, but some web servers (ex: Nginx) do not support CGI.
      If you are using such a web server, you can also run a Fossil
      server using SCGI. See the
      [on-line Fossil SCGI documentation](/doc/trunk/www/scgi.wiki)
      for details.

  1.  **Fossil makes it easy to set up a project website behind a
      reverse proxy.**<p>
      Simply run a [Fossil HTTP server](/doc/trunk/www/server/any/none.md)
      and have your reverse proxy redirect requests to the local server.

  1.  **Fossil makes it easy to set up a project website without
      using any web server at all.**<p>
      The [fossil server command](/help/server) includes a --cert
      option with which you can specify a TLS cert for encrypted
      communication, and with the option, Fossil will except ordinary
      HTTPS requests from the open internet.  There is no need to
      install and configure a separate web server.

  1.  **A single Fossil server is able to host multiple projects.**<p>
      By default, each Fossil server provides content for a single
      repository.  But you can launch a Fossil server that hosts
      multiple repositories by putting all those repositories in a
      directory and giving the directory name as the object to serve
      instead of the repository name.  For this mode of operation,
      the repositories must be named with the "<tt>.fossil</tt>" file
      suffix.  Individual projects have URLs that begin with the
      repository base name, omitting the "<tt>.fossil</tt>" suffix.

  1.  **Fossil automatically pushes after each commit, by default**.<p>
      The [autosync setting](/help/autosync), which defaults to "on", causes
      every commit to automatically push to the default remote.  This helps
      to keep the remote up-to-date and helps all the developers working on
      the software keep up with what other developers are doing.
      <p>
      Long-time Git users might cringe at autosync, thinking that this
      could cause problems if another developer commits ahead of you.
      True enough, that would cause headaches for Git, but it does not
      create problems for Fossil.
      The worse that could happen is that the branch will fork.  Fossil
      will usually detect an impending fork and warn you.
      But even if you don't get the warning (due to a race) or even if you
      override the warning and force the commit anyhow, a fork on a branch
      in Fossil is harmless.  It shows cleanly on the timeline and
      is easily resolved.  So while autosync might cause issues with Git,
      it is harmless to Fossil.  Since forks are harmless, the benefits
      of autosync far outweigh the risks.

  1.  **Fossil supports embedded Pikchr in Wiki and in the Forum.**</p>
      [Pikchr](https://pikchr.org/) is a 
      [PIC-like](https://en.wikipedia.org/wiki/Pic_language) markup language
      for diagram.  The diagrams that appear in the Fossil documentation are
      all drawn using Pikchr.

  1.  **Fossil has a Pikchr sandbox for experimenting with Pikchr scripts.**
      <p>
      The [pikchrshow page](/pikchrshow) allows users to experiment with and
      refine their Pikchr diagrams prior to copy/pasting them into their
      documents.

  1.  **The Fossil web interface supports "embedded documentation"**</p>
      See the [Project Documentation](/doc/trunk/www/embeddeddoc.wiki) page
      for details.  Markdown, Wiki, plain-text, and HTML files in the
      source tree can be rendered and used as documentation pages.  This
      is how all of the documentation files for Fossil itself are rendered.

  1.  **Fossil has a wiki sandbox for experimenting with markup.**</p>
      The [wiki sandbox](/wikiedit?name=Sandbox) allows users to experiment
      with Markdown or other text markup languages supported by Fossil,
      without making permanent changes to the repository.

  1.  **Fossil supports interwiki hyperlinks.**<p>
      Core Git does not have any kind of markup language.  Check-in comments
      in Git are always displayed verbatim.  But wrappers such as GitHub,
      GitLab, Gitea, Forgejo, and similar generally support some variant
      of Markdown.  However, none of these system support links (apart from
      full URLs) to other wiki systems.  Fossil does support interwiki links
      using the syntax "<tt>remote:path</tt>".  So, for example, a checkin
      in the SQLite source repository (<https://sqlite.org/src>) that wants
      to reference a bug report (perhaps because it fixes the bug) can
      include a link of the form "<tt>bugs:/info/</tt><i>HASH</i>" to
      reference that bug.  The source repository knows that "bugs:" refers
      to the [SQLite Bug Forum](https://sqlite.org/bugs) and completes the
      link accordingly.
      <p>
      Interwiki links are important because (1) they help keep hyperlink
      shorter, thus helping to avoid typos, and (2) if the target wiki
      ever moves to a new domain, all the links can be automatically
      adjusted using a server setting rather than causing all of the links
      to go stale, and (3) clones can have different mappings for
      interwiki links, so that (for example) a "bugs:" link in a clone
      of the source repository can map to a clone of the Bugs Forum
      repository.

  1.  **Fossil has a built-in chat server.**<p>
      Users with appropriate permissions (usually just known and registered
      developers, not anonymous passers-by) can bring up a web-based chat
      server on any Fossil web-server instance.  This feature allow
      geographically distributed developers to collaborate interactively,
      without having to involve a third-party chat provider such as Slack.
      Nothing need to be configured in order to activate Chat, other
      that enabling the Chat privilege on the permission bits of the
      users whom you want to have access to Chat.

  1.  **Fossil chat can be configured to send automatic notifications
      when changes occur in the repository.**<p>
      This helps developers keep up with what is happening in the repository.
      The chat window beeps (or not, configurable individually by each user)
      when new messages arrive, as an alert.

  1.  **The Fossil Chat system has hooks that allow external subsystems
      to inject chat message.**<p>
      The SQLite developers use this to get notifications of testing
      failures from our fuzz testing infrastructure.  It could also
      be leveraged to get chat notifications of CI/CD problems.

  1.  **Fossil chat is able to send attachments.**<p>
      When the SQLite developers are working collaboratively on a problem
      (while sitting, literally, in three different continents) we easily
      send patches or diffs to one another over Chat.

  1.  **Fossil supports hyperlinks in checkin comments.**<p>
      Check-in comments need not be just verbatim text (though they can
      be depending on repository settings).  By default, checkin
      comments can contain hyperlinks, including hyperlinks to
      wiki pages, prior checkins, forum posts, and interwiki hyperlinks.

  1.  **Fossil supports hyperlink back references**<p>
      If the checkin comment for a newer commits contains a hyperlink
      back to an older commit, then when the web interface show the
      details of the older commit, it also provides are forward
      reference to the newer commit.

  1.  **Fossil supports a graphical timeline display of a bisect.**<p>
      [For example](https://sqlite.org/src/timeline?bid=y2f0bde4bc8-ndfc790f998-ye2634e500c-yff205f2993-y6bb717acf7-nb48d951916-y8364d89c3b-n98a53fb276-y9d68971c58-y498ee8d514-n043ff54fb7-ye33da6d5dc).
      This is not strictly necessary to make effective use of bisect, but
      the graphical display does seem to help with situational awareness.

  1.  **Fossil can show you the first release in which a particular
      checkin appears, in a single mouse click.**<p>
      You have to configure the repository by giving it the name of the
      tag that you use to mark releases, using the
      [path-to-tag setting](/help/path-to-tag).  Suppose you use the tag
      name "release".  Once you do that, then
      when your are looking at the "info" page for a checkin, a link
      named "path-to-release" appears in the overview section, and if
      you click on that link, it brings up a new graph showing the
      shortest path from that checkin to the next descendant checkin
      tagged with "release".
      <p>
      Example: On the info page at 
      <https://sqlite.org/src/info/b1d7123bc619e3cb>, in the Overview
      section at the top, to the right of the "Timelines:" label, you
      will see the "path-to-release" link.  Click that link to take you
      to a page showing an abbreviated path from the original checkin
      to the first "release" checkin that contains the change.  To
      See the full path, uncheck the "Brief" box near the top of the page.
      <p>
      This feature is useful for when you bisect to find a bug, or a bug
      fix, and want to know the first release in which that bug or bug fix
      appeared.

  1.  **Fossil allows you to revise a checkin comment without
      rewriting history.**<p>
      If you find a typo or other error in an historical checkin comment,
      you can fix the problem in Fossil without having to rewrite all
      subsequent history.  The Fossil file format allows you to set
      a special tag on the checkin you want to revise (actually a
      "property", not a "tag", since it also carries a value - the new
      comment text).  The new tag causes both the command-line display
      and the web interface to show the revised checkin comment rather
      that the original.  Note that the original checkin comment is
      preserved, so there is still an immutable audit trail.  But for
      common use cases, only the new revised comment is shown.
      <p>
      If you are using the web interface and if you have checkin privilege
      on the repository, then on the /info page for the checkin, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets you change
      the checkin comment from the web interface.  This is the easiest
      way to make the change.
      <p>
      See [Fossil checkin b63d654041](/info/b63d65404) for an
      example.  The original comment is shown in the "Overview"
      section of the checkin details, but the revised comment is show
      in the timeline.

  1.  **Fossil allows you to revise a checkin timestamp without
      rewriting history.**<p>
      When generating a new checkin, Fossil uses the current time on the
      system where the commit is occurring.  But if the system clock on
      that system is incorrect, that can lead to a checkin with an
      inaccurate timestamp.  It can be the case that prior checkins
      have later timestamps or that subsequent checkins can have
      earlier timestamps, resulting in goofy-looking "time-warps" in the
      timeline.  This can be correct by add a timestamp correction tag
      to the faulty checkin to fix the timestamp.
      <p>
      If you are using the web interface and if you have checkin privilege
      on the repository, then on the /info page for the checkin, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets you change
      the timestamp for a checkin from the web interface.  This is the
      easiest way to make the change.

  1.  **Fossil allows you to move a checkin to a new branch without
      rewriting history.**<p>
      If you mistakenly commit to the wrong branch, you can move that
      checkin to a new branch by attaching a special tag.
      Note, however, that this will also move all subsequent checkins
      to that same new branch.
      <p>
      In the SQLite and Fossil projects, when developers mistakenly commit
      on the wrong branch, the usual way we fix that is to move the
      mistaken checkin to a branch named "mistake".  Sometimes we also
      set the "hidden" tag on that checkin as well, so that it does not
      show up on ordinary timelines (though it is still part of the
      immutable audit history and is visible with special options).
      Then we just redo the commit on the correct branch.
      <p>
      If you are using the web interface and if you have checkin privilege
      on the repository, then on the /info page for the checkin, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets move the checkin
      to a new branch from the web interface.  You can also do this
      from the Fossil commit-line, but the web interface is easier and less
      error prone.

  1.  **Fossil supports unversioned files**.<p>
      [Unversioned Files](/doc/trunk/www/unvers.wiki) are files held
      in the repository but which are not versioned are which are not
      synced by default.  Unversioned files are used by Fossil itself
      to store [Precompiled Binaries of Fossil](/uv/download.html).
      <p>
      Unversioned contain is not synced *by default*.  But it will
      sync if you add the -u option to the [fossil sync command](/help/sync).
      There are also the [fossil uv sync](/help/uv) command.

  1.  **Fossil automatically selects checkin background colors according to
      the branch that each checkin occurs on.**<p>
      This helps to make the timeline easier to read at a glance, by
      clearly showing which checkins are on which branches.  Developers
      can assign specific colors to branches either when the branch is
      first created, or after the branch has been running for a while.
      But experience teaches us that it is better to just let the web
      interface pick the branch colors automatically.  The color is
      derived from a hash of the branch name.

  1.  **The Fossil web interface contains a page that allows you to
      preview what colors Fossil will choose for branch names.**<p>
      On the [/hash-color-test page](/hash-color-test), one can enter
      candidate branch names and see in advance what colors Fossil will
      pick for that branch name.  This seems like cheating, but I will
      admit that I do this myself, sometimes...

  1.  **The Fossil web interface timeline can be asked to pick checkin
      colors according to the name of the committer, rather than the branch
      name.**<p>
      Simply add the "ubg" query parameter (mnemonic: User BackGround) and
      the checkin colors will be determined by the committer login name
      rather than the branch name or any preselected color name.  This
      results in a timeline that gives reader a clearer view of who is
      making changes.
      [Example](/timeline?n=200&y=ci&ubg).

  1.  **Fossil tracks cherrypick merges.**<p>
      Cherrypicks are recorded as part of the underlying
      [Fossil file format](/doc/trunk/www/fileformat.wiki).
      Cherrypicks appear on the timeline as thin dashed lines.

  1.  **Fossil draws arrows pointing forwards in time.**<p>
      Forward-pointing arrows are far more intuitive than arrows
      that point backwards in times, like Git uses.  Yes, I am aware
      that the underlying implementation of Git has pointers going from child
      to parent, and thus must necessarily go backwards in time.  Fossil
      has the same pointers.  But just because the *implementation*
      points backwards in time does not mean that the *user interface*
      needs to do the same.  Fossil flips those pointers around so that
      they make more sense from the perspective of the human reader.

  1.  **Fossil implements some unix-like shell commands to use as
      substitutes on systems that don't have them or that have inferior
      implementations.**<p>
      For example the "fossil system ls" command works like the standard
      "ls" command on unix.  It doesn't support all the options that a typical
      "ls" implementation supports on Linux, but it is still way better than
      having to run "dir". Other substitute commands include
      "date", "pwd", "stty", "unzip", "which", and "zip".  Probably
      more will be added as needs arise.  Having ready access to these
      commands built into the standalone Fossil binary makes working on
      non-Linux platforms more comfortable for unix geeks, and saves having
      to hunt around and install system-specific alternatives.

  1.  **Fossil can transfer all uncommited changes from a check-out on a
      remote system over to a check-out on the local machine.**<p>
      The command is [fossil patch pull](/help/patch).  It contacts the
      remote system via SSH, updates its local check-out to the same baseline
      as is found on the remote, then pulls over a minimal set of diffs and
      applies them.<p>
      This is very useful in pre-commit testing.  For example, if you have
      a big change on your desktop, and you want to test it before
      committing, on multiple platforms, you can ssh over to those other
      platforms and run "fossil patch pull ... && make test".  When working
      on SQLite, I will typically do that on a remote Mac, and remote
      Win11 machine, and on a 32-core remote Linux machine that runs
      faster than my desktop.

  1.  **Fossil can push uncommitted changes to a remote check-out for the
      same project.**<p>
      This is the same as the previous but in reverse.  It is used, for
      example, to push proposed changes up to a secure sandbox to be
      reviewed by Claude/Codex/Copilot prior to commit.  The sandbox is
      not able to pull, for security reasons, but it can accept a push.

  1.  **Fossil lets you set up aliases for remote checkouts with which
      you commonly push or pull.**<p>
      My desktop is named "r21" and I normally do SQLite development
      work in the direcctory ~/sqlite/sqlite.  If I have uncommitted
      changes that I want to test on Windows, I SSH over to the Win11
      machine then run a commands like:<pre>
        fossil patch pull r21:sqlite/sqlite -f
        make clean test</pre>
      But typing in "r21:sqlite/sqlite" can be tedious and error-prone,
      maybe not so much in this particular examples, but definitely the
      case for longer hostnames and subdirectory paths.  Fortunately,
      Fossil allows us to define patch alias.  On my Win11 machine,
      the alias named "@" is defined as "r21:sqlite/sqlite" and so I
      can get by with typing just:<pre>
        fossil patch pull @ -f</pre>
      (Aside: the -f option tells the command to first "revert" any
      uncommitted changes prior currently in the checkout prior to pulling
      over the new ones from r21:sqlite/sqlite.  Without that option, the
      changes would be merged.)
      

  1.  **Fossil allows you view uncommitted changes on a remote machine
      in a web browser over SSH.**<p>
      Suppose you have some edits on a remote, headless machine and you
      would like to review those changes.  Fossil lets you see those
      changes using a command like:<pre>
        fossil ui remote:path/to/checkout</pre>
      Substitute the name of your remote and the path to the checkout
      on the remote, of course.<p>
      The way this works is that Fossil opens an SSH connection to the
      remote machine that runs [fossil server](/help/server) on the
      remote and that tunnels the HTTP content back through SSH connection
      to your desktop.  At the same time, Fossil brings up your default
      web browser and points it to the local end of your SSH tunnel.

  1.  **Fossil lets you browse a repository on a remote, headless machine
      over an SSH connection.**<p>
      Just run "<tt>fossil ui remote:path/to/repository</tt>" (substituting
      in the name of the remote machine and the path to the repository you
      want to browse, of course) and Fossil will pop up a new page on your
      default web browser (on your desktop!) that is connected over an
      SSH tunnel to that remote repository.

  1.  **Fossil lets you see all repositories on a remote, headless machine
      all at once, over SSH.**<p>
      The command is "<tt>fossil ui remote:/</tt>".  Substitute the name of
      the remote machine, of course.  The special pathname "/" indicates to
      Fossil that you want to view all of the repositories on that machine.
      It causes Fossil to consult the $HOME/.config/fossil.db file
      (or %LOCALAPPDATA%/_fossil on Windows) to find the locations of all
      repositories on the remote machine, then bring up a web page listing
      all those repositories.  Links on that initial page let you explore
      deeper into the details of each repository.
