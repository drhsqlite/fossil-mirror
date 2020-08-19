# Fossil JavaScript Policy

# <span style='color: red'>THIS IS A DRAFT DOCUMENT.</span>

<span style='color: red'>IT IS NOT, IN ITS
CURRENT STATE, TO BE UNDERSTOOD AS OFFICIAL PROJECT STANCE.</span>

The topic of using ECMAScript (better known as JavaScript, abbreviated
JS) in fossil's UI has always been a mildly contentious point. On the
one hand, the site "can" get by fine with purely static HTML, and some
users consider that to be not only adequate, but preferable. On the
other, some level of client-side-only interactivity or UI enhancement
is often useful (some would say preferable), and the only option for
implementing such things in HTML-based interfaces is JS.

The purpose of this document is to explain and justify how JS is used
within the Fossil project.

# Current Uses of JS in Fossil

A brief summary of its current uses in this project:

- To add qualify-of-life enhancements such as the ability to copy
  artifact hashes to the system clipboard.

- To provide asynchronous communication between the client and the
  server, commonly known as "ajax" or "XHR" communication. This allows
  certain pages to operate more quickly and fluidly by avoiding
  complete round-trips to and from the server. Perhaps
  counter-intuitively, the increase JS load such pages typically
  require costs less bandwidth than is saved via using ajax traffic
  instead of conventional HTML forms.

- To implement, or reimplement, certain editing-centric features with
  a degree of interactivity which is impossible to duplicate in purely
  static pages. For example, the summer of 2020 saw the introduction
  of [the /fileedit page](fileedit-page.md), which allows the editing,
  in the browser, of SCM-controlled text files. Similarly, the wiki
  editor was reimplemented to be JS-centric, improving the editing
  capabilities enormously compared to the previously static form-based
  interface.

# Arguments Against JS and Rebuttals

A brief summary of the common arguments *against* using JS, in no
particular order, along with rebuttals against each of them:

1. "It's increases the size of the page download."
  - For the fossil pages which make heavy use of JS, the initial page
    transfer size may increase: 6-8kb (compressed) is typical, and it
    may even go up to a whopping 15kb (as of this writing, it's 8kb
    compressed on our most JS-intensive page (`/fileedit`), and only
    25kb uncompressed and unobfuscated). With fossil's newer (summer
    2020) JS delivery mechanism and etags-related caching
    improvements, such JS can be served with a single HTTP request and
    cached by browsers for up to a year. Additionally, most pages
    which use that much JS also use comparitively lightweight ajax
    communication to eliminate page reloads and enable data-loss-free
    recovery in certain error cases which would lose client-side edits
    in a non-JS-powered page. The end result is that the aggregate
    cost of such pages is actually *lower* than their static
    counterparts, and the total bytes of JS "overhead" is equal to
    only 1-2 full round-trip requests of the equivalent static pages.

2. "It's insecure."
  - JS is historically associated with some nefarious uses, but that's
    a clear case of "hate the game, not the player." (C, fossil's main
    implementation language, has been associated with far more
    security leaks and such than JS has, as have several other
    programming languages.) *Every byte* of JS code used within the
    fossil UI is either written by the fossil developers or closely
    vetted by them, every byte of it is open source, and every byte of
    it is compiled directly into the fossil binary, in a
    non-obfuscated form, during the build process, so there are no
    third-party servers delivering mysterious, obfuscated JS code to
    the user unless an administrator specifically installs some in
    [their repository's skin](customskin.md). Additionally, fossil's
    [default CSP](defcsp.md) prohibits execution of JS code which is
    delivered from anywhere but the fossil server which delivers the
    page.

3. "It's slow."
  - It *was*, before September 2008. Google's introduction [of their
    V8 JS engine][v8] taught the world that JS need not be slow, and
    the JS engines used by every modern browser have been improved
    upon by leaps and bounds to keep them competitive with Google's
    engine. Nowadays JS is, as a rule, astoundingly fast. As the world
    continues to move more and more to web-based applications
    and services, JS engine developers have ample motivation to keep
    their engines fast and competitive.

[v8]: https://en.wikipedia.org/wiki/V8_(JavaScript_engine)

4. "Cross-browser compatibility is poor."
  - It *most certainly was*. Starting around 2006/2007, when jQuery
    literally revolutionized how people worked with and thought about
    JS, there has been a massive industry-level push behind it and
    compatibility has become the norm rather than the exception.
    Cross-browser JS compatibility issues which affect web developers
    are, by and large, a thing of the past.

5. "The UI works fine without it."
  - True, for *some* definition of "works." Modern times and modern
    tools call for modern solutions. While we don't claim to be
    cutting-edge technologists, the days of when static HTML
    form-driven sites were the norm are long behind us. "It works
    fine" is simply not a good justification for holding back on
    notable improvements when they're within easy reach.

6. "JS doesn't run in my text-mode browser."
  - Frankly, neither do other most websites. A man goes to the doctor
    and says, "doc, it hurts when I do this," and the doctor replies,
    "then don't do that."


In closing, the fossil developers want to see fossil *thrive*, and a
small part of that is making it usable, and user-friendly, for a wider
audience than the relatively small segment of users who would prefer
that it remain completely static. Static forms were perfectly adequate
for users in the 1990s, but modern users generally expect a smoother
experience than that and modern developers generally want to write
more interesting code than that.

JS is *not* a perfect solution, but it's what we have and, frankly,
modern editions of the language work very well (though some of the
HTML DOM APIs are admittedly somewhat wonky, they are, with
vanishingly few exceptions, cross-browser compatible and fast).


# Compatibility Concerns

We aim to remain relatively compatible with the largest portions of
the client-side browser base. We use only standards-defined JS code
constructs or constructs which are known to work in the overwhelmingly
vast majority of browsers going back at least approximately 5
years. Features added to the language less than approximately 5 years
agom, or those which are still in flux in standards committees, are
avoided, as it historically takes at least 5 years for new features to
propagate through the various browsers and their users. ECMA6
a.k.a. ES6 a.k.a. ECMAScript 2015 provides a feature-rich basis which
is more than adequate for our purposes.

On a related note: a fantastic resource for guaging the availibility
of individual JS, HTML, and CSS features is
[caniuse.com](https://caniuse.com/).


# Future Plans for JS in Fossil

As of mid-2020, the (very informal) proverbial plan is to increase the
fossil UI's use of JS *considerably* compared to its historically
minimal use of the language. To that end, a framework of
fossil-centric APIs is being developed in conjunction with new features, to
consolidate fossil's historical hodge-podge of JS snippets into a coherent
code base.

When deciding which features to port to JS, the rules of thumb for
this ongoing effort are:

- Pages which primarily display data, e.g. the timeline, will remain
  largely static HTML. Though JS *can* be used effectively to power
  all sorts of wonderful data presentation, fossil currently doesn't
  benefit greatly from doing so, so "data-presentation pages" get by
  with static HTML and maybe a smidgen of JS. e.g., the timeline's
  graph is implemented in JS, as is table sorting on several pages,
  but those are all "nice-to-haves" which improve the experience but
  do not define it.

- Pages which act as editors of some sort, e.g. wiki pages
  (`/wikiedit`), files (`/fileedit`), and checkin information (via
  `/info`), are prime candidates for reimplementing in JS, and the two
  first examples in that list were fossil's first JS-centric pages:
  `/fileedit` was implemented from the ground up in JS and `/wikiedit`
  was reimplemented from a static form-driven app. Similarly, a
  JS-driven overhaul is planned for the forum, initially to add
  JS-driven post editors (from there we'll see what
  can/should/shouldn't be reimplemented in JS).

Those are, however, simply guidelines, not immutable rules. Our
directions of development always boil down to, in order of general
priority:

1) Features the developers themselves want to have and/or work
on.

2) Features end users request which catch the interest of one or
more developers, provided the developer(s) in question are in a
position to expend the effort.

3) Features end users and co-contributors can convince a developer
into coding even when they really don't want to ;).

Even so, fossil's project lead understandably has the final say-so in
whether any given feature indeed gets merged into the mainline trunk,
so development of any given feature, no matter how much effort was
involved, does not guaranty its eventual inclusion into the public
releases.
