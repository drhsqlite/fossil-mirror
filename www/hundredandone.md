# 101 Reasons Why Fossil Is Better Than Git

*This is a work in progress.  I only have 38 reasons typed in so far,
but I have a separate text file of notes that lists 104 candidate reasons.
It's just taking me a while to compose and edit the rationale for each
one, so it seems expedient to check-in this draft.*

  1.  **Fossil comes as a single executable file**.<p>
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
      (for example using "gitk" or other third-party extensions), but
      nothing with anything close to the capabilities of the Fossil
      timeline is available via a web interface, as far as I know.<p>
      Using the web interface, one can easily check on the status
      of a project when away from the office and without access to a clone
      of the repository, just by visiting a link using
      any web browser, even from a phone.

  1.  **In Fossil, a repository is distinct from a working check-out.**<p>
      A Fossil repository can be colocated with the working check-out, as
      they are required to be in Git.  But most people keep the repository
      separate.  One common pattern is to put all Fossil repositories in
      a single directory name $HOME/Fossils or $HOME/Museum, and then open 
      working  check-outs against each repository whereever they are needed.

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
      A user-visible staging area adds no capabilities.  (Partial commits
      are accomplished in Fossil simply by listing the subset of files to
      be committed on the "fossil commit" command line.)  But the staging
      area does complicate the mental model of the project that Git users
      need to keep up with, forcing the developer to spend more time thinking
      about the version control system and hence less time thinking about
      the project they are working on.

  1.  **Fossil remembers where all your repositories and working
      check-outs are located.**<p>
      This and other information (such as all your global settings)
      is stored in a per-user database file
      at $HOME/.config/fossil.db on unix or in %LOCALAPPDATA%/_fossil on
      Windows.  Fossil creates and manages that file automatically; you
      the user never have to touch it or even know it exists.  If you
      move or rename repositories or check-outs, the database will get
      temporarily out of sync with reality, but Fossil will automatically
      resynchornize the next time you do anything with the file or check-out
      that was moved or renamed.<p>
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

  1.  **Fossil allows multiple check-ins to have the same tag.**<p>
      For example, on the SQLite project, every release is tagged with
      "release".

  1.  **The Fossil timeline can show all check-ins with a specific tag.**<p>
      For example, to see all SQLite releases visit
      <https://sqlite.org/src/timeline?t=release> or to see all
      Fossil releases go to
      <https://fossil-scm.org/home/timeline?t=release>.

  1.  **Fossil has a built-in wiki**.<p>
      Wiki pages are colocated in the same repository as your code, so that
      they push, pull, sync, and clone together with your code.

  1.  **Fossil can associated a wiki page with a particular check-in.**<p>
      Rather than including a massive and verbose check-in comment on an
      important check-in (such as the merge of a big new feature), Fossil
      allows you to assign a wiki page to that check-in.  That wiki page
      is show as part of the check-in information in the web interface.
      See, for example, the "About" section of 
      [SQLite check-in 2019-11-21T18:28:44.463Z](https://sqlite.org/src/info/2019-11-21T18:28:44.463Z).
      The wiki page associated with a check-in can be created and revised
      after the check-in is comimtted and pushed.  So it can be used, for
      example, to add notes or bug reports that occur long after the
      check-in itself has been inserted into the DAG.

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
      started last year?"  That branch still exists and you
      can [see it on GitHub](https://github.com/sqlite/sqlite/commits/setlk-snapshot-fix)
      and [in Fossil](https://sqlite.org/src/timeline?r=setlk-snapshot-fix).
      Notice, thought, that GitHub does not show how the branch was resolved.
      Fossil clearly shows that the branch ended up being merged into trunk.
      GitHub just shows us all ancestors of the leaf node labeled
      "setlk-snapshot-fix", including ancestors that were in other branches
      that got merged in, and ancestors that predate the founding of the
      setlk-snapshot-fix branch.
      <p>
      GitHub just cannot shows you the check-ins of branch setlk-snapshot-fix
      only.  There are other third-party tools that will show you that, I am
      told, but they all require a local clone of the repository.  Apparently
      there is no way to see this information in a web browser runnig on
      your phone.

  1.  **Fossil allows multiple branches with the same name.**<p>
      This feature is used a lot for branches named "experimental" and
      "mistake".  See
      <https://sqlite.org/src/timeline?r=experimental> and
      <https://sqlite.org/src/timeline?r=mistake>.  Or see
      [the empty-table-optimizations branch(es)](https://sqlite.org/src/timeline?r=empty-table-optimizations) which was initially merge to trunk and closed
      on 2025-07-02, but then reopened and continued with more enhancements
      until it was merged again on 2025-07-08.  

  1.  **Fossil allows legacy SHA1 hashes and newer SHA3-256 hashes in the
      same repository.**<p>
      Both Fossil and Git started out using SHA1 hashes.  But when the
      SHAttered attack](https://www.marc-stevens.nl/research/shattered.io/)
      against SHA1 was published on 2017-02-23, the need to migrate to a
      stronger hash algorithm was recognized.  Fossil added the ability
      to use SHA3-256 as an alternative on 2017-03-01, and to this day
      it continues to support both, though SHA3-256 is the default for all
      new repositories and check-ins.  But older check-ins that occurred
      prior to ShAttered still use their original SHA1 hash and so no
      repositories had to be rebuilt and no hyperlinks were broken.<p>
      In contrast, a Git repository supports only one hash algorithm.
      Newer repos can use SHA2, though the default is still SHA1.
 
  1.  **Fossil allows check-ins to be identified by timestamp**<p>
      The canonical name for a check-in is its hash.  Both Git and Fossil
      allow a check-in to be identified by any unique prefix of its hash.
      But only Fossil allows a check-in to also be identified by its
      timestamp.  The names "2026-02-02T16:03:24.852Z" and "fdebbedbd9a99165"
      both refer to [the same check-in](https://sqlite.org/src/fdebbedbd9a99165)
      in SQLite, but the first one has the advantage of giving some time
      context rather than just being a seemingly random sequence of hexadecimal
      digits.  It is possible that two or more check-ins can have the same
      timestamp, in which case the timestamp would be ambiguous.  And a
      check-in timestamp can be changed after it is committed, by using
      a special tag.  So timestamp identifiers do not have the uniqueness
      and stability guarantees as hash identifiers, but they are available
      as an option and are often useful.

  1.  **Fossil has a built-in forum.**<p>
      The forum content replicated via push, pull, sync, and clone just like
      source code.  Forum posts can be enabled per user or for all user or
      for all users and anonymous passers-by.  Some user can be appointed
      as moderators and posts from untrusted users can be held for
      moderation.
      <p>
      The [Fossil Forum](https://fossil-scm.org/forum), the
      [SQLite User Forum](https://sqlite.org/forum), and the
      [SQLite Bugs Forum](https://sqlite.org/bugs) are all forums
      set up for specific purposes and deliberately kept separate
      from source code.  But for other projects such as
      [Pikchr](https://pikchr.org/) the Forum and the source code
      are colocated in the same repository.  Thus when you clone the
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
      GitLab or similar, but the setup and maintenance is somewhat more
      involved.  You'll also probably need a bigger machine if you are
      using GitLab, whereas a Fossil website
      works fine on a 2GiB Raspberry PI or a $6/month VPS.  In fairness,
      I am told that Gitea and Forgejo also work well on a small machine.

  1.  **Fossil automatically pushes after each commit, by default**.<p>
      The [autosync setting](/help/autosync), which defaults to "on", causes
      every commit to automatically push to the default remote.  This helps
      to keep the remote up-to-date and helps all the developers working on
      the software keep up with what other developers are doing.
      <p>
      Users whose prior experience is only with Git might object to
      autosync, saying that this could cause all kinds of problems for Git
      if another developer commits ahead of you.  True enough, that would
      cause headaches for Git, but it does not cause any problems for Fossil.
      The worse that could happen is that the branch will fork.  Fossil
      will usually detect that a fork is about to happen and warn you.
      But even if you don't get the warning (due to a race) or even if you
      override the warning and force the commit anyhow, a fork on a branch
      in Fossil is quite harmless.  It shows cleanly on the timeline and
      is easily resolved.  So while autosync might case issues with Git,
      it does not cause issues in Fossil and so the benefits far outweigh
      the risks and autosync is the default behavior.

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
      using the syntax "<tt>remote:path</tt>".  So, for example, a check-in
      in the SQLite source repository (<https://sqlite.org/src>) that wants
      to reference a bug report (perhaps because it fixes the bug) can
      include a link of the form "<tt>bugs:/info/</tt><i>HASH</i>" to
      reference that bug.  The source repository knows that "bugs:" refers
      to the [SQLite Bug Forum](https://sqlite.org/bugs) and completes the
      link accordingly.
      <p>
      Interwiki links are important because (1) they help keep hyperlink
      shorter, thus helping to avoid typos, and (2) if the target wiki
      ever modes to a new domain, all the links can be automatically
      adjusted using a server setting rather than causing all of the links
      to go stale, and (3) clone can have different mappings for
      interwiki links, so that (for example) a "bugs:" link in a clone
      of the source repository can map to a clone of the Bugs Forum
      repository.

  1.  **Fossil has a built-in chat server**<p>
      Users with appropriate permissions (usually just known and registered
      developers, not anonymous passers-by) can bring up a web-based chat
      server on any Fossil web-server instance.  This feature allow
      geographically distributed developers to collaborate interactively,
      without having to involve a third-party chat provider such as Slack.
      Fossil can be configured to automatically report new check-ins and
      other activities as chat messages, so that developers are alerted to
      changes.  This feature is built-in to every Fossil web interface
      and it works with any modern web browser.  No extra plugins or
      JS frameworks are required.  Nothing need to be configured, other
      that enabling the Chat privilege on the permission bits of the
      users whom you want to have access to Chat.

  1.  **Fossil supports hyperlinks in check-in comments.**<p>
      Check-in comments need not be just verbatim text (though they can
      be depending on repository settings).  By default, check-in
      comments can contain hyperlinks, including hyperlinks to
      wiki pages, prior check-ins, forum posts, and interwiki hyperlinks.

  1.  **Fossil supports hyperlink back references**<p>
      If the check-in comment for a newer commits contains a hyperlink
      back to an older commit, then when the web interface show the
      details of the older commit, it also provides are forward
      reference to the newer commit.

  1.  **Fossil supports a graphical timeline display of a bisect.**<p>
      [For example](https://sqlite.org/src/timeline?bid=y2f0bde4bc8-ndfc790f998-ye2634e500c-yff205f2993-y6bb717acf7-nb48d951916-y8364d89c3b-n98a53fb276-y9d68971c58-y498ee8d514-n043ff54fb7-ye33da6d5dc).
      This is not strictly necessary to make effective use of bisect, but
      the graphical display does seem to help with situational awareness.

  1.  **Fossil allows you to revise a check-in comment without
      rewriting history.**<p>
      If you find a typo or other error in an historical check-in comment,
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
      If you are using the web interface and if you have check-in privilege
      on the repository, then on the /info page for the check-in, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets you change
      the check-in comment from the web interface.  This is the easiest
      way to make the change.
      <p>
      See [Fossil check-in b63d654041](/info/b63d65404) for a
      recent example.  The original comment is shown in the "Overview"
      section of the checkin details, but the revised comment is show
      in the timeline.

  1.  **Fossil allows you to revise a check-in timestamp without
      rewriting history.**<p>
      When generating a new checkin, Fossil uses the current time on the
      system where the commit is occurring.  But if the system clock on
      that system is incorrect, that can lead to a check-in with an
      inaccurate timestamp.  It can be the case that prior check-ins
      have later timestamps or that subsequent check-ins can have
      earlier timestamps, resulting in goofy-looking "time-warps" in the
      timeline.  This can be correct by add a timestamp correction tag
      to the faulty check-in to fix the timestamp.
      <p>
      If you are using the web interface and if you have check-in privilege
      on the repository, then on the /info page for the check-in, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets you change
      the timestamp for a check-in from the web interface.  This is the
      easiest way to make the change.

  1.  **Fossil allows you to move a check-in to a new branch without
      rewriting history.**<p>
      If you mistakenly commit to the wrong branch, you can move that
      check-in to a new branch by attaching a special tag.
      Note, however, that this will also move all subsequent check-ins
      to that same new branch.
      <p>
      In the SQLite and Fossil projects, when developers mistakenly commit
      on the wrong branch, the usual way we fix that is to move the
      mistaken check-in to a branch named "mistake".  Sometimes we also
      set the "hidden" tag on that check-in as well, so that it does not
      show up on ordinary timelines (though it is still part of the
      immutable audit history and is visible with special options).
      Then we just redo the commit on the correct branch.
      <p>
      If you are using the web interface and if you have check-in privilege
      on the repository, then on the /info page for the check-in, under
      the "Overview" section, to the right of "Other Links:", there is
      an "edit" link that will take you to a page that lets move the check-in
      to a new branch from the web interface.  You can also do this
      from the Fossil commit-line, but the web interface is easier and less
      error prone.

  1.  **Fossil supports unversioned files**.<p>
      [Unversioned Files](/doc/trunk/www/unvers.wiki) are files held
      in the repository but which are not versioned are which are not
      synced by default.  Unversioned files are used by Fossil itself
      to store [Precompiled Binaries of Fossil](/uv/download.html).

  1.  **Fossil automatically select checkin background colors according to
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

  1.  **The Fossil web interface timeline can be asked to pick check-in
      colors according to the name of the committer, rather than the branch
      name.**<p>
      Simply add the "ubg" query parameter (mnemonic: User BackGround) and
      the checkin colors will be determined by the committer login name
      rather than the branch name or any preselected color name.  This
      results in a timeline that gives reader a clearer view of who is
      making changes.
      [Example](/timeline?n=200&y=ci&ubg).
