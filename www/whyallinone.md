# Why Add Forum, Wiki, and Web Software To Your DVCS?

One notable feature of Fossil is that it bundles
[bug tracking](./bugtheory.wiki),
[wiki](./wikitheory.wiki),
[forum](./forum.wiki),
[chat](./chat.md), and
[technotes](./event.wiki)
with distributed version control to give you an
all-in-one software project management system.

A commenter on [Hacker News](https://news.ycombinator.com/item?id=27437895)
takes exception to this idea, writing:

>  *I don't want forum/web software built into my dvcs.*
>  *I don't see how this improves over git.*

The commenter may hold whatever opinions they want, of course.
However, here are a few reasons why bundling other project management
features with the DVCS might be useful for a given project:

  1.  There is single software package to install and manage for the
      project website.
      The alternative is to select, install, configure, learn about,
      manage, and maintain separate DVCS, wiki,
      ticketing, forum,
      chat, documentation, and whatever other software packages your project needs.
      Less time spent on project administration details means more
      time available to spend on the project itself.

  2.  Because of Fossil’s autosync feature, you get a backup of the
      wiki, tickets, forum, and so forth simply by cloning the
      repository to another machine and using that clone regularly.
      Since the typical Fossil usage pattern is to stand the repo up on a
      central server and have the developers clone that repository down
      to their personal machines, if the server falls over, the last
      developer to do anything that resulted in an autosync has a
      functional and up-to-date backup.

      There are [limitations to using Fossil’s autosync feature for
      backup purposes](./backup.md), but that document gives two methods
      for more complete backups, both of which are easily automated. The
      Fossil project itself is distributed across three data centers in
      this manner via cron.

  3.  Remote workers get more than just the source code:
      they get the entire website including versioned documentation,
      wiki articles, tickets, forum posts, and so forth. This supports
      off-network development when traveling, when riding out Internet
      service failures, and when workers must sync with multiple remote
      servers, as when working alternately from home and in some central
      office.

      Feature-competitive Fossil alternatives typically solve this same
      problem with centralization, which generally means that only the
      DVCS piece still works in these situations where the developer is
      unable to contact the central server. Why accept the limitation of
      having a distributed clone of the code repo alone?

      Centralization doesn’t work for every project. If you enjoy the
      benefits of truly distributed (read: non-centralized) version
      control, you may also benefit from distributed forums, distributed
      ticket tracking, distributed wiki article publishing, and so
      forth.

  4.  Integration of all of these features allows easy hyperlinks between 
      check-in comments, wiki pages, forum posts, and tickets. More,
      because the software sees both sides of the link, referrer and
      referent, it can provide automatic back-references.

      A common situation in a Fossil project is that:

      * a forum post refers to a versioned project document that shows
        that the software isn’t behaving as documented;
      * a developer triages that forum report as a verified bug, filing
        a ticket to prioritize and track the resolution;
      * developers chat about the problem, referring to the ticket and
        thereby indirectly referring to the forum post, plus perhaps to
        other Fossil-managed resources such as a wiki document giving
        design principles that guide the proper fix; and finally
      * the commit message resolving the ticket includes a reference to
        the ticket it resolves.

      Since Fossil sees that the commit refers to a ticket, the ticket
      page automatically also refers back to the commit, closing the
      loop. A latecomer may arrive at the ticket via a web search, and
      from that see that it was closed following a commit. They can
      follow the link from the initial ticket message to the forum
      thread to catch up on the discussion leading to the fix and likely
      find a follow-up post from the initial reporting user saying
      whether the fix worked for them. If further work was needed, the
      latecomer can likely find it from that forum thread.

      This works even in a remote off-network clone: the developer can
      pull up the project web site via an `http://localhost` link and
      follow these links around the loop.

      Fossil allows breaking some of these project facilities out into
      separate repositories, as when the public forum is kept separate
      from the actual software development repository for administration
      reasons. By using Fossil’s [interwiki link
      feature](./interwiki.md), you can get this same internal linking
      from ticket to commit to forum to wiki even across these
      administrative  boundaries, even with remote off-network clones,
      simply by adjusting the interwiki map to match the remote clone’s
      network configuration.

  5.  The forum and chat features of Fossil are disabled by default, and
      you can disable the ticket-tracking and wiki features with a quick
      configuration change to its [role-based access control
      system](./caps/), allowing you to treat Fossil as a more direct
      drop-in for Git. When you’re ready to turn these features on, you
      can do so with a few mouse clicks.

      Because Fossil is web-native out of the box, if you’ve delegated
      these features to outside systems to flesh out Git’s DVCS-only
      nature, Fossil can link out to these systems, and they back into
      Fossil.

  6.  Bundling all of these services gives [single sign-on][SSO] (SSO) for all
      aspects of the project.  The same username/password works for code,
      wiki, forum, tickets, and chat.

      If you choose to administratively separate some of these features
      by setting up multiple cooperating Fossil repositories, its [login
      groups feature](./caps/login-groups.md) allows asymmetric SSO
      across these administrative boundaries so that, for example, users
      allowed to commit to the code repository also get a forum
      repository login, but self-registered forum users don’t
      automatically get the ability to commit to the code repo.

  7.  Bundling all of these features reduces the number of external
      dependencies for the project.

      Take the first two points above: standing up a Fossil repo backup
      on a new server may be as simple as copying the backup to the new
      server and [configuring its stock HTTP server to point at the
      backup repository via CGI](./server/any/cgi.md).

      Consider: if you had good backups for all of the elements in a
      Git + [Jira] + [Discord] + [MediaWiki] + [Sphinx] lash-up, how long
      would it take you to stand up a replacement? That lash-up
      certianly has more features combined than Fossil alone, but are
      they worth the administration and hosting costs they impose?

      Considerations such as these push many into centralized hosting
      servides such as GitHub, GitLab, Bitbucket, and so forth, but that
      just takes you back to point 3 above.

  8.  Hosting all of these elements within a single service gives a
      consistent look-and-feel across all aspects of the project, including
      [project-specific extensions](./serverext.wiki).
      The [SQLite Release Checklist][srckl] is a noteworthy example of this.

  9.  Fossil is [free, open-source software](../COPYRIGHT-BSD2.txt),
      through and through. Git-backed lash-ups tend to incorporate
      either proprietary add-ons or proprietary hosting systems that
      produce vendor lock-in. Fossil gives you the freedom to take your
      complete backup (point 2) of the project including its
      idiosyncratic customizations and stand it up elsewhere on
      commodity hardware and software stacks.

[Discord]:   https://discord.com/
[Jira]:      https://www.atlassian.com/software/jira
[MediaWiki]: https://www.mediawiki.org/
[Sphinx]:    https://www.sphinx-doc.org/en/master/
[SSO]:       https://en.wikipedia.org/wiki/Single_sign-on
[srckl]:     https://www.sqlite.org/src/ext/checklist/top/index
