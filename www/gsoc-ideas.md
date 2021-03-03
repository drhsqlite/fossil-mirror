# Project Ideas for Google Summer of Code 2021

This list was made for the Fossil project's application for [Google Summer of
Code](https://summerofcode.withgoogle.com/) in 2021. GSoC pays students to
contribute to free software projects during the Northern Hemiphere summer.  If
you are a student, you will be able to apply for GSoC starting March 29th 2021.

This page applies to the two implementations of Fossil: [the classic Fossil](https://fossil-scm.org)
and [libfossil](https://fossil.wanderinghorse.net/r/libfossil). The two implementations 
have an identical implementation of the Fossil data model, are 100% compatible in terms of
data access since they use the same SQL, and are 100% binary compatible in terms of on-disk storage.

## General Features

* Complete per-feature CSS facilities in [the Inskinerator](https://tangentsoft.com/inskinerator/dir) and add features to the Inskinerator
* Improve the documentation history-browsing page to enable selection of 2 arbitrary versions to diff, similar to the [Mediawiki history feature enabled on Wikipedia](https://en.wikipedia.org/w/index.php?title=Fossil_(software)&action=history)
* Allow diffing of Forum posts
* Develop a test suite for the draft JSON API in libfossil. This JSON API is a way of integrating many kinds of systems with Fossil
* Re-implement the draft JSON API in libfossil to use the JSON capability in SQLite, now that SQLite has JSON. This is a large project and would start with feasibility analysis
* Fossil hooks for pipelines with CI/CD such as static analysis, Buildbot, Gerrit, Travis and Jenkins are not well-documented and may need some further development. Make this work better, with configuration examples
* Create a [Pandoc](https://pandoc.org) filter that handles Fossil-style Markdown
* Create a [Pandoc filter that handles Pikchr](https://groups.google.com/g/pandoc-discuss/c/zZSspnHHsg0?pli=1) (Pikchr can be used with many kinds of layout, not just Markdown)
* Editor integration: [improve the Fossil VSCode plugin](https://marketplace.visualstudio.com/items?itemName=koog1000.fossil) or [create a Fossil plugin for Eclipse](https://marketplace.eclipse.org/taxonomy/term/26%2C31)

## Add code to handle email bounces

Fossil can [send email alerts](./alerts.md), but cannot receive email at all. That is a good thing, because a 
complete [SMTP MTA](https://en.wikipedia.org/wiki/MTA) is complicated and requires constant maintenance. There
is one specific case where receiving mail in some fashion would help, and that is for handling bounce messages
from invalid email addresses. 

A proposal for that is to implement a Fossil command such as:

```
fossil email -R repo receive_bounce
```

This is a non-network-aware Mail Delivery Agent, and would be called by an MTA such as Postfix, Courier or Exim.
This command would reject anything that doesn't look like a bounce it is expecting.

## Work relating to the ticketing system in Fossil

The Fossil SCM project uses tickets in a [somewhat unusual manner](https://fossil-scm.org/home/reportlist)
because the social programming
model has evolved to often use the Fosum instead.  Other Fossil-using projects
use tickets in a more traditional report-a-bug manner. So this means that the
Fossil ticketing system user interface is underdeveloped. On the other hand,
pretty much every software developer uses a ticketing system at some point in
their workflow, and Fossil is intended to be usable by most developers.  The
underlying technology for the Fossil ticketing system is guaranteed, so to
improve it requires only user interface changes.

Projects relating to the ticketing system include:

* Improving the [Fossil cli for tickets](https://fossil-scm.org/forum/forumpost/d8e8a1cf92) which is confusing, as pointed out in that ticket.
* Alternatively, instead of improving Fossil's cli, implement a comprehensive ticket commandline with [libfossil's primitives](https://fossil.wanderinghorse.net/r/libfossil/wiki/home), look under the f-apps/ directory.
* Improving the Fossil web UI for ticketing, which is clunky to say the least

# Look and Feel

Tasks for those interested in graphic/web design:

* General touch-ups in the existing skins. This may, depending on how deep one
  cares to dig, require digging into C code to find, and potentially modify, how
  the HTML is generated.
* Creation of one or more new skins. This does not specifically require any C
  know-how.

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


