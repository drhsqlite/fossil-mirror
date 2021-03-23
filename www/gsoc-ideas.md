# List of Projects and Tasks

This list was made for the Fossil project's application for [Google Summer of Code](https://summerofcode.withgoogle.com/) in 2021. That application was
unsuccessful, but still this list is a starting point for anyone looking
for a place to start. We welcome newcomers, and invite developers to follow the simple
[procedures for contributing to Fossil](./contribute.wiki). The
[hacker how-to](./hacker-howto.wiki) is recommended reading.

There are two implementations of the Fossil data model:

* [the classic Fossil project](https://fossil-scm.org) , which is where this file is maintained and 
  which is as of 2021 how everyone interacts with Fossil objects 
* [libfossil](https://fossil.wanderinghorse.net/r/libfossil), which is an independent project to manipulate Fossil objects from a library, or using commandline tools which are thin wrappers to the library

As of 2021 the two implementations have an identical implementation of the
Fossil data model, are 100% compatible in terms of data access since they use
the same SQL, and are 100% binary compatible in terms of on-disk storage.

The projects listed here are grouped by functionality - User Interface, Integration, Email,
etc. If you are looking for something easy to start with, then depending where
your interests lie, there are some small libfossil tasks and small
features to work on in the UI.

# UI, Look and Feel

Tasks for those interested in graphic/web design:

* Add a quote button to the Forum, such as [discussed in this thread](https://fossil-scm.org/forum/forumpost/7ad03cd73d)
* Improve the documentation history-browsing page to enable selection of 2 arbitrary versions to diff, similar to the [Mediawiki history feature enabled on Wikipedia](https://en.wikipedia.org/w/index.php?title=Fossil_(software)&action=history)
* Allow diffing of Forum posts
* General touch-ups in the existing skins. This may, depending on how deep one
  cares to dig, require digging into C code to find, and potentially modify, how
  the HTML is generated.
* Creation of one or more new skins. This does not specifically require any C
  know-how.
* Complete per-feature CSS facilities in [the Inskinerator](https://tangentsoft.com/inskinerator/dir) and add features to the Inskinerator

# Projects Relating to Fossil Integration

* Fossil hooks for pipelines with CI/CD such as static analysis, Buildbot, Gerrit, Travis and Jenkins are not well-documented and may need some further development. Make this work better, with configuration examples
* Create a [Pandoc](https://pandoc.org) filter that handles Fossil-style Markdown
* Create a [Pandoc filter that handles Pikchr](https://groups.google.com/g/pandoc-discuss/c/zZSspnHHsg0?pli=1) (Pikchr can be used with many kinds of layout, not just Markdown)
* Editor integration: [improve the Fossil VSCode plugin](https://marketplace.visualstudio.com/items?itemName=koog1000.fossil) or [create a Fossil plugin for Eclipse](https://marketplace.eclipse.org/taxonomy/term/26%2C31)
* Develop a test suite for the draft JSON API in libfossil. This JSON API is a way of integrating many kinds of systems with Fossil
* Re-implement the draft JSON API in libfossil to use the JSON capability in SQLite, now that SQLite has JSON. This is a large project and would start with feasibility analysis

# Adding Inbound (Receiving) Email to Fossil

This task involves designing a new feature and working with Fossil developers to 
see how it can be feasible in practice.

Fossil can [send email alerts](./alerts.md), but cannot receive email at all.
That is a good thing, because a complete [SMTP
MTA](https://en.wikipedia.org/wiki/MTA) is complicated and requires constant
maintenance, so Fossil should not try to be an MTA or ever listen to mail ports
on the Internet. 

There is one specific type of email reception that make sense for Fossil to
handle.  When there is inbound mail related to a message that Fossil has
previously generated with a unique hash, Fossil already knows the context of
that message.  An unknown sender cannot guess a valid hash although a malicious
sender could of course find a way to receive a valid hash and then use that to
gain access.  The risk of automatic and non-specific spam is very low. 

A proposal to handle that would be to implement a Fossil command like this:

```
fossil email -R repo receive -t TYPE-OF-EMAIL -h HASH
```

Where the type of email would be one of a list something like this:

* mail_bounce
* ticket_reply
* forum_reply

This command is a non-network-aware [Mail Delivery
Agent](https://en.wikipedia.org/wiki/Mail_delivery_agent), and would be called
by an SMTP MTA such as Postfix, Courier or Exim. The MTA would need to be
configured to recognise that this is an email intended for Fossil, and what
type of email, and to extract its hash.  People who configure MTAs are used to
doing this sort of thing, but no doubt Fossil would include a sample
[Postfix mail filter](http://www.postfix.org/FILTER_README.html#simple_filter) and 
an equivalent driver for Exim.

The Fossil command would reject anything that doesn't look like a bounce it is expecting.

It is not certain that this design is the best one to address the inbound mail
problem. That is why the first part of this task is to find a workable design.

# Work relating to the ticketing system in Fossil

The Fossil SCM project uses tickets in a [somewhat unusual manner](https://fossil-scm.org/home/reportlist)
because the social programming
model has evolved to often use the Forum instead of ticketing.  Other Fossil-using projects
use tickets in a more traditional report-a-bug manner. So this means that the
Fossil ticketing system user interface is underdeveloped.

On the other hand, pretty much every software developer uses a ticketing system
at some point in their workflow, and Fossil is intended to be usable by most
developers. That means the ticketing system really needs to be further
developed. The underlying technology for the Fossil ticketing system is
guaranteed, so to improve it requires only user interface changes.

Projects relating to the ticketing system include:

* Improving the [Fossil cli for tickets](https://fossil-scm.org/forum/forumpost/d8e8a1cf92) which is confusing, as pointed out in that ticket. This is still classified as a "user interface" even though it isn't graphical.
* Alternatively, instead of improving Fossil's cli, implement a comprehensive ticket commandline with [libfossil's primitives](https://fossil.wanderinghorse.net/r/libfossil/wiki/home), look under the f-apps/ directory.
* Improving the Fossil web UI for ticketing, which is clunky to say the least. Fossil tries not be a heavy user of Javascript and Javascript libraries, but the wikiedit, chat and Forum code are all more advanced than ticketing, 
and have UI features that would improve ticketing
* If there is an inbound email system as per the previous section "Adding Inbound (Receiving) Email to Fossil", then implement this system for ticketing

# Tasks Requiring Fossil Data Model Knowledge

The Fossil data model concepts are simple, but the implications are quite subtle and impressive. The data model
is designed to [endure for centuries](./fileformat.wiki),
be [easily accessible](./fossil-v-git.wiki#durable), and is [non-relational](./fossil-is-not-relational.md).
You will need to understand the data model to work on the following tasks:

* Add the ability to tag non-checkin artifacts, something supported by
  the data model but not the current CLI and UIs. This would open the
  door to numerous new features, such as "sticky" forum posts and
  per-file extended attributes. This could also relate to the RBAC
  system.
* Implement "merge" and "stash" in libfossil
* Analyse the different kinds of [split/export/shallow clone](https://fossil-scm.org/forum/forumpost/1aa4f8ea8c6f96) use cases for Fossil including [complete bifurcation](https://fossil-scm.org/forum/forumpost/6434a06871). There are many proposals, relating to many different use cases, and a good analysis would help us to work out what should be implemented, and what should be implemented in Fossil and what is instead a libfossil wrapper

# Fossil is cool

There are many reasons why Fossil is just plain cool:

* Fossil is symbiotically connected with [SQL and SQLite](5631123d66d96)
* Fossil is highly portable accross different operating systems
* Fossil is the [only credible alternative to Git](./fossil-v-git.wiki)
* Fossil is both ultra-long-term stable and has a high rate of development and new features
* Fossil has thought deeply about Comp Sci principles including [CAP Theorem](./cap-theorem.md) and [whether Fossil is a blockchain](./blockchain.md)
* Fossil has two independent implementations of the same data model: Fossil and libfossil

and a lot, lot more, in the source, docs, forum and more.




``` pikchr center toggle 
// Click to see the rendered diagram this describes,
// written in Fossil's built-in pikchr language, see https://pikchr.org
// 
// based on pikchr script by Kees Nuyt, licensed
// https://creativecommons.org/licenses/by-nc-sa/4.0/

scale = 1.0
eh = 0.5cm
ew = 0.2cm
ed = 2 * eh
er = 0.4cm
lws = 4.0cm
lwm = lws + er
lwl = lwm + er

ellipse height eh width ew fill Bisque color CadetBlue 
L1: line width lwl from last ellipse.n
line "click for" bold above width lwm from last ellipse.s
LV: line height eh down

move right er down ed from last ellipse.n
ellipse height eh width ew fill Bisque color CadetBlue 
L3: line "example of Fossil" bold width lws right from last ellipse.n to LV.end then down eh right ew
line width lwm right from last ellipse.s then to LV.start

move right er down ed from last ellipse.n
ellipse height eh width ew fill Bisque color CadetBlue 
line width lwl right from last ellipse.n then to L1.end
line "coolness" bold width lwl right from last ellipse.s then up eh

```


