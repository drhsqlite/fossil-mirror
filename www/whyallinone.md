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

The commenter hold whatever opinions he wants, of course. However, here
are a few reasons why bundling other project management software with
the DVCS might be useful:

  1.  There is single software package to install and manage for the
      project website.
      The alternative is to select, install, configure, learn about,
      manage, and maintain separate software packages for DVCS, wiki,
      tickets, forum,
      chat, documentation, and whatever else your project needs.
      Less time spent on project administration details means more
      time available to spend on the project itself.

  2.  Easily back-up the wiki, tickets, forum, and so forth using "sync".
      You can do this manually, or automatically using a cron job.

  3.  People who clone the project get more than just the source code -
      they get the entire website, including documentation,
      wiki, tickets, forum, and so forth.  Download a project and
      take it off-network with you when you travel.

  4.  Support for hyperlinks between 
      check-in comments, wiki pages, forum posts, tickets, with
      back-references.

  5.  Nobody forces you to use the parts of the system that you do not
      want to use. Begin with only DVCS enabled. Turn on other components
      later, as needs arise, using a few simple mouse clicks.

  6.  Single sign-in for all aspects of the project.  The same
      username/password works for code, wiki, forum, tickets, and chat.

  7.  Reduce the number of external dependencies for the project.

  8.  Consistent look-and-feel across all aspects of the project, including
      [project-specific extensions](./serverext.wiki).
      The [SQLite Release Checklist][8] is a noteworthy example of this.

[8]: https://www.sqlite.org/src/ext/checklist/top/index
