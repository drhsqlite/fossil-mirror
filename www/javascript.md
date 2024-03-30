# Use of JavaScript in Fossil

## Philosophy & Policy

The Fossil development project’s policy is to use JavaScript where it
helps make its web UI better, but to offer graceful fallbacks wherever
practical. The intent is that the UI be usable with JavaScript
entirely disabled. In almost all places where Fossil uses JavaScript,
it is an enhancement to provided functionality, and there is always
another way to accomplish a given end without using JavaScript.

This is not to say that Fossil’s fall-backs for such cases are always as
elegant and functional as a no-JS purist might wish. That is simply
because [the vast majority of web users leave JavaScript unconditionally
enabled](#stats), and of the small minority of those that do not, a
large chunk use some kind of [conditional blocking](#block) instead,
rather than disable JavaScript entirely.
Fossil’s active developers do not deviate from that
norm enough that we have many no-JS purists among us, so the no-JS case
doesn’t get as much attention as some might want. We do [accept code
contributions][cg], and we are philosophically in favor of graceful
fall-backs, so you are welcome to appoint yourself the position of no-JS
czar for the Fossil project!

Evil is in actions, not in objects: we do not believe JavaScript *can*
be evil. It is an active technology, but the actions that matter here
are those of writing the code and checking it into the Fossil project
repository. None of the JavaScript code in Fossil is evil, a fact we
enforce by being careful about who we give check-in rights on the
repository to and by policing what code does get contributed. The Fossil
project does not accept non-trivial outside contributions.

We think it’s better to ask not whether Fossil requires JavaScript but
whether Fossil uses JavaScript *well*, so that [you can decide](#block)
to block or allow Fossil’s use of JavaScript.

The Fossil developers want to see the project thrive, and we achieve
that best by making it usable and friendly to a wider audience than the
minority of static web app purists.  Modern users generally expect a
smoother experience than was available with 1990s style HTTP
POST-and-response `<form>` based interaction. We also increase the set
of potential Fossil developers if we do not restrict them to such
antiquated methods.

JavaScript is not perfect, but it's what we have, so we will use it
where we find it advantageous.

[cg]: ./contribute.wiki


## <a id="debate"></a>Arguments Against JavaScript & Our Rebuttals

There are many common arguments against the use of JavaScript. Rather than
rehash these same arguments on the [forum][ffor], we distill the common
ones we’ve heard before and give our stock answers to them here:

1.  “**It increases the size of the page download.**”

    The heaviest such pages served by Fossil only have about 15 kB of
    compressed JavaScript. (You have to go out of your way to get Fossil
    to serve uncompressed pages.) This is negligible, even over very
    slow data connections. If you are still somehow on a 56 kbit/sec
    analog telephone modem, this extra script code would download in
    a few seconds.

    Most JavaScript-based Fossil pages use less code than that.

    Atop that, Fossil sends HTTP headers to the browser that allow it
    to perform aggressive caching so that typical page loads will skip
    re-loading this content on subsequent loads. These features are
    currently optional: you must either set the new
    [`fossil server --jsmode bundle` option][fsrv] or the corresponding
    `jsmode` control line
    in your [`fossil cgi`][fcgi] script when setting up your
    [Fossil server][fshome]. That done, Fossil’s JavaScript files will
    load almost instantly from the browser’s cache after the initial
    page load, rather than be re-transferred over the network.

    Between the improved caching and the fact that it’s quicker to
    transfer a partial Ajax page load than reload the entire page, the
    aggregate cost of such pages is typically *lower* than the older
    methods based on HTTP POST with a full server round-trip. You can
    expect to recover the cost of the initial page load in 1-2
    round-trips. If we were to double the amount of JavaScript code in
    Fossil, the payoff time would increase to 2-4 round-trips.

2.  “**JavaScript is slow.**”

    It *was*, before September 2008. Google's introduction of [their V8
    JavaScript engine][v8] taught the world that JavaScript need not be
    slow. This competitive pressure caused the other common JavaScript
    interpreters to either improve or be replaced by one of the engines
    that did improve to approach V8’s speed.

    Nowadays JavaScript is, as a rule, astoundingly fast. As the world
    continues to move more and more to web-based applications and
    services, JavaScript engine developers have ample motivation to keep
    their engines fast and competitive.

    Ajax partial page updates are faster than
    the no-JS alternative, a full HTTP POST round-trip to submit new
    data to the remote server, retrieve an entire new HTML document,
    and re-render the whole thing client-side.

3.  <a id="3pjs"></a>“**Third-party JavaScript cannot be trusted.**”

    Fossil does not use any third-party JavaScript libraries, not even
    very common ones like jQuery. Every bit of JavaScript served by the
    stock version of Fossil was written specifically for the Fossil
    project and is stored [in its code repository][fsrc].

    Therefore, if you want to hack on the JavaScript code served by
    Fossil and mechanisms like [skin editing][cskin] don’t suffice for your
    purposes, you can hack on the JavaScript in your local instance
    directly, just as you can hack on its C, SQL, and Tcl code. Fossil
    is free and open source software, under [a single license][2cbsd].

4.  <a id="snoop"></a>“**JavaScript and cookies are used to snoop on web users.**”

    There is no tracking or other snooping technology in Fossil other than
    that necessary for basic security, such as IP address logging on
    check-ins. (This is in part why we have no [comprehensive user
    statistics](#stats)!)

    Fossil attempts to set two cookies on all web clients: a login session
    cookie and a display preferences cookie. These cookies are restricted to
    the Fossil instance, so even this limited data cannot leak between
    Fossil instances or into other web sites.

5.  “**JavaScript is fundamentally insecure.**”

    JavaScript is certainly sometimes used for nefarious ends, but if we
    wish to have more features in Fossil, the alternative is to add more
    code to the Fossil binary, [most likely in C][fslpl], a language
    implicated in [over 4× more security vulnerabilities][whmsl].

    Therefore, does it not make sense to place approximately four times
    as much trust in Fossil’s JavaScript code as in its C code?

    The question is not whether JavaScript is itself evil, it is whether
    its *authors* are evil. *Every byte* of JavaScript code used within
    the Fossil UI is:

    *   ...written by the Fossil developers, vetted by their peers.

    *   ...[open source][flic] and [available][fsrc] to be inspected,
        audited, and changed by its users.

    *   ...compiled directly into the `fossil` binary in a
        non-obfuscated form during the build process, so there are no
        third-party servers delivering mysterious, obfuscated JavaScript
        code blobs to the user.

    Local administrators can [modify the repository’s skin][cskin] to
    inject additional JavaScript code into pages served by their Fossil
    server. A typical case is to add a syntax highlighter like
    [Prism.js][pjs] or [highlightjs][hljs] to the local repository. At
    that point, your trust concern is not with Fossil’s use of
    JavaScript, but with your trust in that repository’s administrator.

    Fossil's [default content security policy][dcsp] (CSP)
    prohibits execution of JavaScript code which is delivered from
    anywhere but the Fossil server which delivers the page. A local
    administrator can change this CSP, but again this comes down to a
    matter of trust with the administrator, not with Fossil itself.

6.  “**Cross-browser compatibility is poor.**”

    It most certainly was in the first decade or so of JavaScript’s
    lifetime, resulting in the creation of powerful libraries like
    jQuery to patch over the incompatibilities. Over time, the need for
    such libraries has dropped as browser vendors have fixed the
    incompatibilities.  Cross-browser JavaScript compatibility issues
    which affect web developers are, by and large, a thing of the past.

7.  “**Fossil UI works fine today without JavaScript. Why break it?**”

    While this is true today, and we have no philosophical objection to
    it remaining true, we do not intend to limit ourselves to only those
    features that can be created without JavaScript. The mere
    availability of alternatives is not a good justification for holding
    back on notable improvements when they're within easy reach.

    The no-JS case is a [minority position](#stats), so those that want
    Fossil to have no-JS alternatives and graceful fallbacks will need
    to get involved with the development if they want this state of
    affairs to continue.

8.  <a id="stats"></a>“**A large number of users run without JavaScript enabled.**”
  
    That’s not what web audience measurements say:

    * [What percentage of browsers with javascript disabled?][s1]
    * [How many people are missing out on JavaScript enhancement?][s2]
    * [Just how many web users really disable cookies or JavaScript?][s3]

    Our sense of this data is that only about 0.2% of web users had
    JavaScript disabled while participating in these studies.

    The Fossil user community is not typical of the wider web, but if we
    were able to comprehensively survey our users, we’d expect to find
    an interesting dichotomy. Because Fossil is targeted at software
    developers, who in turn are more likely to be power-users, we’d
    expect to find Fossil users to be more in favor of some amount of
    JavaScript blocking than the average web user. Yet, we’d also expect
    to find that our user base has a disproportionately high number who
    run [powerful conditional blocking plugins](#block) in their
    browsers, rather than block JavaScript entirely. We suspect that
    between these two forces, the number of no-JS purists among Fossil’s
    user base is still a tiny minority.

9.  <a id="block"></a>“**I block JavaScript entirely in my browser. That breaks Fossil.**”

    First, see our philosophy statements above. Briefly, we intend that
    there always be some other way to get any given result without using
    JavaScript, developer interest willing.

    But second, it doesn’t have to be all-or-nothing. We recommend that
    those interested in blocking problematic uses of JavaScript use
    tools like [NoScript][ns] or [uBlock Origin][ubo] to *selectively*
    block JavaScript so the rest of the web can use the technology
    productively, as it was intended.

    There are doubtless other useful tools of this sort. We recommend
    these two only from our limited experience, not out of any wish to
    exclude other tools.

    The primary difference between these two for our purposes is that
    NoScript lets you select scripts to run on a page on a case-by-case
    basis, whereas uBlock Origin delegates those choices to a group of
    motivated volunteers who maintain allow/block lists to control all
    of this; you can then override UBO’s stock rules as needed.

10. “**My browser doesn’t even *have* a JavaScript interpreter.**”

    The Fossil open source project has no full-time developers, and only
    a few of these part-timers are responsible for the bulk of the code
    in Fossil. If you want Fossil to support such niche use cases, then
    you will have to [get involved with its development][cg]: it’s
    *your* uncommon itch.

11. <a id="compat"></a>“**Fossil’s JavaScript code isn’t compatible with my browser.**”

    The Fossil project’s developers aim to remain compatible with
    the largest portions of the client-side browser base. We use only
    standards-defined JavaScript features which are known to work in the
    overwhelmingly vast majority of browsers going back approximately 5
    years, at minimum, as documented by [Can I Use...?][ciu] We avoid use of
    features added to the language more recently or those which are still in
    flux in standards committees.

    We set this threshold based on the amount of time it typically takes for
    new standards to propagate through the installed base.

    As of this writing, this means we are only using features defined in
    [ECMAScript 2015][es2015], colloquially called “JavaScript 6.” That
    is a sufficiently rich standard that it more than suffices for our
    purposes, and it is [widely deployed][es6dep]. The biggest single
    outlier remaining is MSIE 11, and [even Microsoft is moving their
    own products off of it][ie11x].

[2cbsd]:  https://fossil-scm.org/home/doc/trunk/COPYRIGHT-BSD2.txt
[ciu]:    https://caniuse.com/
[cskin]:  ./customskin.md
[dcsp]:   ./defcsp.md
[es2015]: https://ecma-international.org/ecma-262/6.0/
[es6dep]: https://caniuse.com/#feat=es6
[fcgi]:   /help?cmd=cgi
[ffor]:   https://fossil-scm.org/forum/
[flic]:   /doc/trunk/COPYRIGHT-BSD2.txt
[fshome]: /doc/trunk/www/server/
[fslpl]:  /doc/trunk/www/fossil-v-git.wiki#portable
[fsrc]:   https://fossil-scm.org/home/file/src
[fsrv]:   /help?cmd=server
[hljs]:   https://fossil-scm.org/forum/forumpost/9150bc22ca
[ie11x]:  https://techcommunity.microsoft.com/t5/microsoft-365-blog/microsoft-365-apps-say-farewell-to-internet-explorer-11-and/ba-p/1591666
[ns]:     https://noscript.net/
[pjs]:    https://fossil-scm.org/forum/forumpost/1198651c6d
[s1]:     https://blockmetry.com/blog/javascript-disabled
[s2]:     https://gds.blog.gov.uk/2013/10/21/how-many-people-are-missing-out-on-javascript-enhancement/
[s3]:     https://w3techs.com/technologies/overview/client_side_language/all
[ubo]:    https://github.com/gorhill/uBlock/
[v8]:     https://en.wikipedia.org/wiki/V8_(JavaScript_engine)
[whmsl]:  https://www.whitesourcesoftware.com/most-secure-programming-languages/


----

## <a id="uses"></a>Places Where Fossil’s Web UI Uses JavaScript

This section documents the areas where Fossil currently uses JavaScript
and what it does when these uses are blocked. It also gives common
workarounds where necessary.


### <a id="timeline"></a>Timeline Graph

Fossil’s [web timeline][wt] uses JavaScript to render the graph
connecting the visible check-ins to each other, so you can visualize
parent/child relationships, merge actions, etc. We’re not sure it’s even
possible to render this in static HTML, even with the aid of SVG, due to
the vagaries of web layout among browser engines, screen sizes, etc.

Fossil also uses JavaScript to handle clicks on the graph nodes to allow
diffs between versions, to display tooltips showing local context, etc.

_Graceful Fallback:_ When JavaScript is disabled, this column of the
timeline simply collapses to zero width. All of the information you can
get from the timeline can be retrieved from Fossil in other ways not
using JavaScript: the “`fossil timeline`” command, the “`fossil info`”
command, by clicking around within the web UI, etc.

_Potential Workaround:_ The timeline could be enhanced with `<noscript>`
tags that replace the graph with a column of checkboxes that control
what a series of form submit buttons do when clicked, replicating the
current JavaScript-based features of the graph using client-server round-trips.
For example, you could click two of those checkboxes and then a button
labeled “Diff Selected” to replicate the current “click two nodes to
diff them” feature.

[wt]: https://fossil-scm.org/home/timeline


### <a id="wedit"></a>The New Wiki Editor

The [new wiki editor][fwt] has many new features, a
few of which are impossible to get without use of JavaScript.

First, it allows in-browser previews without losing client-side editor
state, such as where your cursor is. With the old editor, you had to
re-locate the place you were last editing on each preview, which would
reduce the incentive to use the preview function. In the new wiki
editor, you just click the Preview tab to see how Fossil interprets your
markup, then click back to the Editor tab to resume work with the prior
context undisturbed.

Second, it continually saves your document state in client-side storage
in the background while you’re editing it so that if the browser closes
without saving the changes back to the Fossil repository, you can resume
editing from the stored copy without losing work. This feature is not so
much about saving you from crashes of various sorts, since computers are
so much more reliable these days. It is far more likely to save you from
the features of mobile OSes like Android and iOS which aggressively shut
down and restart apps to save on RAM. That OS design philosophy assumes
that there is a way for the app to restore its prior state from
persistent media when it’s restarted, giving the illusion that it was
never shut down in the first place. This feature of Fossil’s new wiki
editor provides that.

With this change, we lost the old WYSIWYG wiki editor, available since
Fossil version 1.24. It hadn’t been maintained for years, it was
disabled by default, and no one stepped up to defend its existence when
this new editor was created, replacing it. If someone rescues that
feature, merging it in with the new editor, it will doubtless require
JavaScript in order to react to editor button clicks like the “**B**”
button, meaning “make \[selected\] text boldface.” There is no standard
WYSIWYG editor component in browsers, doubtless because it’s relatively
straightforward to create one using JavaScript.

_Graceful Fallback:_ Fossil’s lack of
a script-free wiki editor mode is not from lack of
desire, but because the person who wrote the new wiki editor didn’t
want to maintain three different editors. (New Ajaxy editor, old
script-free HTML form based editor, and the old WYSIWYG JavaScript-based
editor.) If someone wants to implement a `<noscript>` alternative to the
new wiki editor, we will likely accept that [contribution][cg] as long
as it doesn’t interfere with the new editor. (The same goes for adding
a WYSIWYG mode to the new Ajaxy wiki editor.)

_Workaround:_ You don’t have to use the browser-based wiki editor to
maintain your repository’s wiki at all. Fossil’s [`wiki` command][fwc]
lets you manipulate wiki documents from the command line. For example,
consider this Vi based workflow:

```shell
$ vi 'My Article.wiki'                   # begin work on new article
  ...write, write, write...
:w                                       # save changes to disk copy
:!fossil wiki create 'My Article' '%'    # current file (%) to new article
  ...write, write, write some more...
:w                                       # save again
:!fossil wiki commit 'My Article' '%'    # update article from disk
:q                                       # done writing for today

  ....days later...
$ vi                                     # work sans named file today
:r !fossil wiki export 'My Article' -    # pull article text into vi buffer
  ...write, write, write yet more...
:w !fossil wiki commit -                 # vi buffer updates article
```

Extending this concept to other text editors is an exercise left to the
reader.

[fwc]: /help?cmd=wiki
[fwt]: ./wikitheory.wiki


### <a id="fedit"></a>The File Editor

Fossil’s [optional file editor feature][fedit] works
much like [the new wiki editor](#wedit), only on files committed to the
repository.

The original designed purpose for this feature is to allow [embedded
documentation][edoc] to be interactively edited in the same way that
wiki articles can be. (Indeed, the associated `fileedit-glob` feature
allows you to restrict the editor to working *only* on files that can be
treated as embedded documentation.) This feature operates in much the
same way as the new wiki editor, so most of what we said above applies.

_Workaround:_ This feature is an alternative to Fossil’s traditional
mode of file management: clone the repository, open it somewhere, edit a
file locally, and commit the changes.

_Graceful Fallback:_ There is no technical reason why someone could not
write a `<noscript>` wrapped alternative to the current JavaScript based
`/fileedit` implementation. It would have all of the same downsides as
the old wiki editor: the users would lose their place on each save, they
would have no local backup if something crashes, etc. Still, we are
likely to accept such a [contribution][cg] as long as it doesn’t
interfere with the new editor.

[edoc]:  /doc/trunk/www/embeddeddoc.wiki
[fedit]: /doc/trunk/www/fileedit-page.md


### <a id="ln"></a>Line Numbering

When viewing source files, Fossil offers to show line numbers in some
cases. ([Example][mainc].) Toggling them on and off is currently handled
in JavaScript, rather than forcing a page-reload via a button click.

_Workaround:_ Manually edit the URL to give the “`ln`” query parameter
per [the `/file` docs](/help?cmd=/file).

_Potential Better Workaround:_ Someone sufficiently interested could
[provide a patch][cg] to add a `<noscript>` wrapped HTML button that
would reload the page with this parameter included/excluded to implement
the toggle via a server round-trip.

A related feature is Fossil’s JavaScript-based interactive method
for selecting a range of lines by clicking the line numbers when they’re
visible. JavaScript lets us copy the resulting URL to the clipboard
to share your selection with others.

_Workaround:_ These interactive features would be difficult and
expensive (in terms of network I/O) to implement without JavaScript.  A
far simpler alternative is to manually edit the URL, per above.

[mainc]: https://fossil-scm.org/home/artifact?ln&name=87d67e745


### <a id="sxsdiff"></a>Side-by-Side Diff Mode

The default “diff” view is a side-by-side mode. If either of the boxes
of output — the “from” and “to” versions of the repo contents for that
check-in — requires a horizontal scroll bar given the box content, font
size, browser window width, etc., both boxes will usually end up needing
to scroll since they should contain roughly similar content. Fossil
therefore scrolls both boxes when you drag the scroll bar on one because
if you want to examine part of a line scrolled out of the HTML element
in one box, you probably want to examine the same point on that line in
the other box.

_Graceful Fallback:_ Manually scroll both boxes to sync their views.

### <a id="diffcontext"></a>Diff Context Loading

Fossil’s diff views can
dynamically load more lines of context around changed blocks. The UI
controls for this feature are injected using JavaScript when the page
initializes and make use of XHR requests to fetch data from the
fossil instance.

_Graceful Fallback:_ The UI controls for this feature do not appear
when JS is unavailable, leaving the user with the "legacy" static diff
view.


### <a id="sort"></a>Table Sorting

On pages showing a data table, the column headers may be clickable to do
a client-side sort of the data on that column.

_Potential Workaround:_ This feature could be enhanced to do the sort on
the server side using a page re-load.


### <a id="tree"></a>File Browser Tree View

The [file browser’s tree view mode][tv] uses JavaScript to handle clicks
on folders so they fold and unfold without needing to reload the entire
page.

_Graceful Fallback:_ When JavaScript is disabled, clicks on folders
reload the page showing the folder contents instead. You then have to
use the browser’s Back button to return to the higher folder level.

[tv]: https://fossil-scm.org/home/dir?type=tree


### <a id="hash"></a>Version Hashes

In several places where the Fossil web UI shows a check-in hash or
similar, hovering over that check-in shows a tooltip with details about
the type of artifact the hash refers to and allows you to click to copy
the hash to the clipboard.

_Graceful Fallback:_ When JavaScript is disabled, these tooltips simply
don’t appear, but you can still select and copy the hash using your
platform’s “copy selected text” feature.


### <a id="bots"></a>Anti-Bot Defenses

Fossil has [anti-bot defenses][abd], and it has some JavaScript code
that, if run, can drop some of these defenses if it decides a given page
was loaded on behalf of a human, rather than a bot.

_Graceful Fallback:_ You can use Fossil’s anonymous login feature to
convince the remote Fossil instance that you are not a bot. Coupled with
[the Fossil user capability system][caps], you can restore all
functionality that Fossil’s anti-bot defenses deny to random web clients
by default.

[abd]:  ./antibot.wiki
[caps]: ./caps/


### <a id="hbm"></a>Hamburger Menu

Several of the stock skins (including the default) include a “hamburger menu” (&#9776;) which uses
JavaScript to show a simplified version of the Fossil UI site map using
an animated-in dropdown.

_Graceful Fallback:_ Clicking the hamburger menu button with JavaScript
disabled will take you to the `/sitemap` page instead of showing a
simplified version of that page’s content in a drop-down.

_Workaround:_ You can remove this button by [editing the skin][cskin]
header.


### <a id="clock"></a>Clock

Some stock Fossil skins include JavaScript-based features such as the
current time of day. The Xekri skin includes this in its header, for
example. A clock feature requires JavaScript to get the time on initial
page load and then to update it once a minute.

You may observe that the server could provide the current time when
generating the page, but the client and server may not be in the same
time zone, and there is no reliably-provided information from the client
that would let the server give the page load time in the client’s local
time zone. The server could only tell you *its* local time at page
request time, not the client’s time. That still wouldn’t be a “clock,”
since without client-side JavaScript code running, that part of the page
couldn’t update once a second.

_Potential Graceful Fallback:_ You may consider showing the server’s
page generation time rather than the client’s wall clock time in the
local time zone to be a useful fallback for the current feature, so [a
patch to do this][cg] may well be accepted. Since this is not a
*necessary* Fossil feature, an interested user is unlikely to get the
core developers to do this work for them.


### <a id="chat"></a>Chat

The [chat feature](./chat.md) is deeply dependent
on JavaScript. There is no obvious way to do this sort of thing without
active client-side code of some sort.

_Potential Workaround:_ It would not be especially difficult for someone
sufficiently motivated to build a Fossil chat gateway, connecting to
IRC, Jabber, etc. The messages are stored in the repository’s `chat`
table with monotonically increasing IDs, so a poller that did something
like

    SELECT xfrom, xmsg FROM chat WHERE msgid > 1234;

…would pull the messages submitted since the last poll. Making the
gateway bidirectional should be possible as well, as long as it properly
uses SQLite transactions.

### <a id="brlist"></a>List of branches

The [`/brlist`](/brlist) page uses JavaScript to enable
selection of several branches for further study via `/timeline`.
Client-side script interactively responds to checkboxes' events
and constructs a special hyperlink in the submenu.
Clicking this hyperlink loads a `/timeline` page that shows
only these selected branches (and the related check-ins).

_Potential Workaround:_ A user can manually construct an appropriate
regular expession and put it into the "Tag Filter" entry of the
`/timeline` page (in its advanced mode).

----

## <a id="future"></a>Future Plans for JavaScript in Fossil

As of mid-2020, the informal provisional plan is to increase Fossil
UI's use of JavaScript considerably compared to its historically minimal
uses. To that end, a framework of Fossil-centric APIs is being developed
in conjunction with new features to consolidate Fossil's historical
hodgepodge of JavaScript snippets into a coherent code base.

When deciding which features to port to JavaScript, the rules of thumb
for this ongoing effort are:

-  Pages which primarily display data (e.g. the timeline) will remain
   largely static HTML with graceful fallbacks for all places they do
   use JavaScript. Though JavaScript can be used effectively to power
   all sorts of wonderful data presentation, Fossil currently doesn't
   benefit greatly from doing so. We use JavaScript on these pages only
   to improve their usability, not to define their primary operations.

-  Pages which act as editors of some sort (e.g. the `/info` page) are
   prime candidates for getting the same treatment as the old wiki
   editor: reimplemented from the ground up in JavaScript using Ajax
   type techniques. Similarly, a JS-driven overhaul is planned for the
   forum’s post editor.

These are guidelines, not immutable requirements. Our development
direction is guided by our priorities:

1) Features the developers themselves want to have and/or work on.

2) Features end users request which catch the interest of one or more
developers, provided the developer(s) in question are in a position to
expend the effort.

3) Features end users and co-contributors can convince a developer into
coding even when they really don't want to.

In all of this, Fossil's project lead understandably has the final
say-so in whether any given feature indeed gets merged into the mainline
trunk. Development of any given feature, no matter how much effort was
involved, does not guarantee its eventual inclusion into the public
releases.
