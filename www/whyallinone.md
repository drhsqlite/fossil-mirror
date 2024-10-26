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

This commenter may hold whatever opinions he wishes, of course.
However, there are many good reasons why bundling other project management
features with the DVCS might be useful for a given project:

  1.  There is a single software package to install and manage for the
      project website.
      The alternative is to select, install, configure, learn about,
      manage, and maintain separate DVCS, wiki,
      ticketing, forum,
      chat, documentation, and whatever other software packages your project needs.
      Less time spent on project administration details means more
      time available to spend on the project itself.

  2.  Fossil’s autosync feature gives you an implicit backup of the
      wiki, tickets, forum, and so forth simply by cloning the
      repository to another machine and using that clone regularly.
      Since the typical Fossil usage pattern is to stand the repo up on a
      central server and have the developers clone that repository down
      to their personal machines, if the server falls over, the last
      developer to do anything that resulted in an autosync has a
      functional and up-to-date backup.

      There are [limitations to relying on Fossil’s autosync feature for
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

  5.  Bundling all of these services gives [single sign-on][SSO] (SSO) for all
      aspects of the project.  The same username/password works for code,
      wiki, forum, tickets, and chat.

      If you choose to administratively separate some of these features
      by setting up multiple cooperating Fossil repositories, its [login
      groups feature](./caps/login-groups.md) allows asymmetric SSO
      across these administrative boundaries so that, for example, users
      allowed to commit to the code repository also get a forum
      repository login, but self-registered forum users don’t
      automatically get the ability to commit to the code repo.

  6.  Bundling all of these features reduces the number of external
      dependencies for the project.

      Take the first two points above: standing up a Fossil repo backup
      on a new server may be as simple as copying the backup to the new
      server and [configuring its stock HTTP server to point at the
      backup repository via CGI](./server/any/cgi.md).

      Consider: If you had good backups for all of the elements in a
      Git + [Jira] + [Discord] + [MediaWiki] + [Sphinx] lash-up, how long
      would it take you to stand up a replacement? That lash-up
      certainly has more features combined than Fossil alone, but are
      they worth the administration and hosting costs they impose?
      Fossil’s feature set suffices for the SQLite project it was
      created to serve, as well as for many others; is your project
      sufficiently more complex, such that it *needs* all of those extra
      features and their concomitant complexity?

      Considerations such as these push many into centralized hosting
      services such as GitHub, GitLab, Bitbucket, and so forth, but that
      just takes you back to point 3 above.

  7.  Hosting all of these elements within a single service gives a
      consistent look-and-feel across all aspects of the project.

      Skinning independent software packages’ web interfaces to make
      them appear unified is more work than [skinning] everything once, as
      in Fossil, and even then, you can’t make independently-developed
      software look like it was produced by a single entity without
      resorting to heroic levels of customization. If you use a separate
      DVCS web front end, chat system, forum manager, documentation
      system, ticket tracker, and so on, you are likely to be relegated
      to simply matching colors and fonts; you *might* also get the
      ability to add a common logo to the header of all of these
      independent pieces. The pieces won’t look unified, because they
      weren’t developed that way.

      The Fossil project’s
      skinning system lets you affect all of its elements globally from the
      single skin editor.

      Or not: there’s a feature in Fossil that lets skin customizations
      apply to only *some* Fossil features. The initial impetus behind
      this feature was that one of our users wanted Markdown to be
      rendered with different indentation in forum posts than in
      [embedded documentation][edoc] owing to the inherent differences between
      the two presentation modalities.

      A user taking advantage of this per-feature CSS capability who
      wishes to change a UI element common to all Fossil features — say,
      to change the font for literal code blocks — may still make such a
      change globally. Opting into this per-feature CSS doesn’t fork all
      skinning efforts: UI elements not explicitly reskinned on a
      per-feature basis inherit the global skinning.

      But it goes futher. Fossil has a feature for [project-specific
      extensions](./serverext.wiki), which backs the [SQLite Release
      Checklist][srckl], for instance. You wouldn’t know by looking at
      that page that it’s produced by software that isn’t actually part
      of Fossil: the extension only delivers the core of the page,
      and Fossil’s skining wraps it in a way that lets it inherit all of
      the project-level skinning customizations.

  8.  Unifying all of these features within Fossil
      means we have a single Markdown interpreter common to all
      elements. If you lash multiple software systems together, even if
      they can all agree on Markdown as a common document markup
      language — hardly a given, as shown by the MediaWiki and Sphinx
      elements in point 6’s example above — they’re likely to render your text
      using different — possibly even incompatibly-different — Markdown
      dialects.

      This costs you in mental gear-switching when moving from the code
      repository to the documentation system to the forums to the ticket
      tracker.

      More than that, though, a developer might write a forum post that later gets
      promoted to a wiki article or to an embedded version-controlled
      project document. A developer on a Fossil-backed project may simply copy-paste the forum post
      text into the new document and save it, not needing to carefully
      check that it still renders properly under the second Markdown
      rendering engine. Similarly, if a user reports a potential bug via
      the forum, the developer can copy interesting pieces of the
      Markdown from the post into a ticket comment, again without
      needing to fiddle with dialect incompatibilities.

  9.  Fossil is [free, open-source software](../COPYRIGHT-BSD2.txt),
      through and through. Git-backed lash-ups tend to incorporate
      either proprietary add-ons or proprietary hosting systems that
      produce vendor lock-in. Fossil gives you the freedom to take your
      complete backup (point 2) of the project including its
      idiosyncratic customizations and stand it up elsewhere on
      commodity hardware and software stacks.

All of this having been said, the non-DVCS features of Fossil are
optional. Its forum and chat features are disabled by default, and you
can disable the ticket-tracking and wiki features with a quick
configuration change to its [role-based access control system](./caps/).
When you’re ready to turn these additional features on, you can do so
with a few mouse clicks.

Because Fossil is web-native out of the box, if you’ve delegated these
features to outside systems to flesh out Git’s DVCS-only nature, you are
free to do the same with Fossil. One of the many things the [skinning]
facility allows is replacing the built-in links to the wiki, forum,
ticket system, etc. with links to external systems. How easy those
systems make it to link back into Fossil is up to their developers.

[Discord]:   https://discord.com/
[edoc]:      ./embeddeddoc.wiki
[Jira]:      https://www.atlassian.com/software/jira
[MediaWiki]: https://www.mediawiki.org/
[skinning]:  ./customskin.md
[Sphinx]:    https://www.sphinx-doc.org/en/master/
[SSO]:       https://en.wikipedia.org/wiki/Single_sign-on
[srckl]:     https://www.sqlite.org/src/ext/checklist/top/index
