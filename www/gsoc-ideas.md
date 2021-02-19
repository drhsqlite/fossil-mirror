# Project Ideas for Google Summer of Code

This list was made for the Fossil project's application for [Google Summer of
Code](https://summerofcode.withgoogle.com/) in 2021. GSoC pays students to
contribute to free software projects during the Northern Hemiphere summer.  If
you are a student, you will be able to apply for GSoC starting March 29th 2021.

## General Features

* Complete per-feature CSS facilities in [the Inskinerator](https://tangentsoft.com/inskinerator/dir) and add features to the Inskinerator
* Improve the documentation history-browsing page to enable selection of 2 arbitrary versions to diff, similar to the Wikipedia (Mediawiki) history feature
* Allow diffing of Forum posts
* Re-implement the draft JSON API in libfossil to use the JSON capability in SQLite, now that SQLite has JSON. This is a large project
* Fossil hooks for pipelines with CI/CD such as static analysis, Buildbot, Gerrit, Travis and Jenkins are not well-documented and may need some further development. Make this work better, with configuration examples
* Create a [Pandoc](https://pandoc.org) filter that handles Fossil-style Markdown
* Create a Pandoc filter that handles Pikchr (Pikchr can be used with many kinds of layout, not just Markdown)
* Editor integration: [improve VSCode](https://marketplace.visualstudio.com/items?itemName=koog1000.fossil) or [create a Fossil plugin for Eclipse](https://marketplace.eclipse.org/taxonomy/term/26%2C31)

## Add code to handle email bounces

Fossil can [send email alerts](./alerts.md), but cannot receive email at all. That is a good thing, because a 
complete [SMTP MTA](https://en.wikipedia.org/wiki/MTA) is complicated and requires constant maintenance. There
is one specific case where receiving mail in some fashion would help, and that is for handling bounce messages
from invalid email addresses. 

A proposal for that is to implement a Fossil command such as:

```
implement "fossil email -R repo receive_bounce"
```

This is a non-network-aware Mail Delivery Agent, and would be called by an MTA such as Postfix, Courier or Exim.
This command would reject anything that doesn't look like a bounce it is expecting.

# Tasks Requiring Fossil Data Model Knowledge

The Fossil data model concepts are simple, but the implications are quite subtle and impressive. The data model
is designed to [endure for centuries](./fileformat.wiki),
be [easily accessible](./fossil-v-git.wiki#durable), and is [non-relational](./fossil-is-not-relational.md).
You will need to understand the data model to work on the following tasks:

* Add the ability tag non-checkin artifacts, something the CLI and UIs do not although the data model does. One suggestion is that this is how xattrs could apply to file blobs. This could also relate to the RBAC system.
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


