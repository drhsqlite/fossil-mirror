# Use of JavaScript in Fossil

## Philosophy

The Fossil development project’s policy is to use JavaScript where it
helps make its web UI better, but to offer graceful fallbacks wherever
practical. The intent is that the UI be usable with JavaScript entirely
disabled.  In every place where Fossil uses JavaScript, it is an
enhancement to provided functionality, and there is always another way
to accomplish a given end without using JavaScript.

This is not to say that Fossil’s fall-backs for such cases are always as
elegant and functional as a no-JS purist might wish. That is simply
because [the vast majority of web users run with JS enabled](#stats),
and a minority of those run with some kind of conditional JavaScript
blocking in place. Fossil’s active developers do not deviate from that
norm enough that we have many no-JS purists among us, so the no-JS case
doesn’t get as much attention as some might want. We do [accept code
contributions][cg], and we are philosophically in favor of graceful
fall-backs, so you are welcome to appoint yourself the position of no-JS
czar for the Fossil project!

Evil is in actions, not in nouns, so we do not believe JavaScript *can*
be evil. It is an active technology, but the actions that matter here
are those of writing the code and checking it into the Fossil project
repository. None of the JavaScript code in Fossil is evil, a fact we
enforce by being careful about who we give check-in rights on the
repository to and by policing what code does get contributed. The Fossil
project does not accept non-trivial outside contributions.

We think it’s better to ask not whether Fossil requires JavaScript but
whether Fossil uses JavaScript *well*, so that [you can decide](#block)
to block or allow Fossil’s use of JavaScript.

[cg]: ./contribute.wiki


## <a id="block"></a>Blocking JavaScript

Rather than either block JavaScript wholesale or give up on blocking
JavaScript entirely, we recommend that you use tools like [NoScript][ns]
or [uBlock Origin][ub] to selectively block problematic uses of
JavaScript so the rest of the web can use the technology productively,
as it was intended. There are doubtless other useful tools of this sort;
we recommend only these two due to our limited experience, not out of
any wish to exclude other tools.

The primary difference between these two for our purposes is that
NoScript lets you select scripts to run on a page on a case-by-case
basis, whereas uBlock Origin delegates those choices to a group of
motivated volunteers who maintain whitelists and blacklists to control
all of this; you can then override UBO’s stock rules as needed.

[ns]: https://noscript.net/
[ub]: https://github.com/gorhill/uBlock/


## <a id="stats"></a>How Many Users Run with JavaScript Disabled Anyway?

There are several studies that have directly measured the web audience
to answer this question:

* [What percentage of browsers with javascript disabled?][s1]
* [How many people are missing out on JavaScript enhancement?][s2]
* [Just how many web users really disable cookies or JavaScript?][s3]

Our sense of this data is that only about 0.2% of web users had
JavaScript disabled while participating in these studies.

The Fossil user community is not typical of the wider web, but if we
were able to comprehensively survey our users, we’d expect to find an
interesting dichotomy. Because Fossil is targeted at software
developers, who in turn are more likely to be power-users, we’d expect
to find Fossil users to be more in favor of some amount of JavaScript
blocking than the average web user. Yet, we’d also expect to find that
our user base has a disproportionately high number who run [powerful
conditional blocking plugins](#block) in their browsers, rather than
block JS entirely. We suspect that between these two forces, the number
of no-JS purists among Fossil’s user base is still a tiny minority.

[s1]: https://blockmetry.com/blog/javascript-disabled
[s2]: https://gds.blog.gov.uk/2013/10/21/how-many-people-are-missing-out-on-javascript-enhancement/
[s3]: https://w3techs.com/technologies/overview/client_side_language/all


## <a id="3pjs"></a>No Third-Party JavaScript in Fossil

Fossil does not use any third-party JavaScript libraries, not even very
common ones like jQuery. Every bit of JavaScript served by the stock
version of Fossil was written specifically for the Fossil project and is
stored [in its code repository](https://fossil-scm.org/fossil/file).

Therefore, if you want to hack on the JavaScript code served by Fossil
and mechanisms like [skin editing][cs] don’t suffice for your purposes,
you can hack on the JavaScript in your local instance directly, just as
you can hack on its C, SQL, and Tcl code. Fossil is free and open source
software, under [a single license][2cbsd].

[2cbsd]: https://fossil-scm.org/home/doc/trunk/COPYRIGHT-BSD2.txt
[cs]:    ./customskin.md


## <a id="snoop"></a>Fossil Does Not Snoop On You

There is no tracking or other snooping technology in Fossil other than
that necessary for basic security, such as IP address logging on
check-ins. (This is in part why we have no [comprehensive user
statistics](#stats)!)

Fossil attempts to set two cookies on all web clients: a login session
cookie and a display preferences cookie. These cookies are restricted to
the Fossil instance, so even this limited data cannot leak between
Fossil instances or into other web sites.

There is some server-side event logging, but that is done entirely
without JavaScript, so it’s off-topic here.


## <a id="uses"></a>Places Where Fossil’s Web UI Uses JavaScript

The remainder of this document will explain how Fossil currently uses
JavaScript and what it does when these uses are blocked.


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
current JS-based features of the graph using client-server round-trips.
For example, you could click two of those checkboxes and then a button
labeled “Diff Selected” to replicate the current “click two nodes to
diff them” feature.

[wt]: https://fossil-scm.org/fossil/timeline


### <a id="wedit"></a>WYSIWYG Wiki Editor

The Admin → Wiki → “Enable WYSIWYG Wiki Editing” toggle switches the
default plaintext editor for [Fossil wiki][fw] documents to one that
works like a basic word processor. This feature requires JavaScript in
order to react to editor button clicks like the “**B**” button, meaning
“make \[selected\] text boldface.” There is no standard WYSIWYG editor
component in browsers, doubtless because it’s relatively straightforward
to create one using JavaScript.

_Graceful Fallback:_ Edit your wiki documents in the default plain text
wiki editor. Fossil’s wiki and Markdown language processors were
designed to be edited that way.

[fw]: ./wikitheory.wiki


### <a id="ln"></a>Line Numbering

When viewing source files, Fossil offers to show line numbers in some
cases. Toggling them on and off is currently handled in JavaScript.
([Example][mainc].)

_Workaround:_ Edit the URL to give the “`ln`” query parameter per [the
`/file` docs](/help?cmd=/file), or provide a patch to reload the page
with this parameter included/excluded to implement the toggle via a
server round-trip.

[mainc]: https://fossil-scm.org/fossil/artifact?ln&name=87d67e745


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

[tv]: https://www.fossil-scm.org/fossil/dir?type=tree


### <a id="hash"></a>Version Hashes

In several places where the Fossil web UI shows a check-in hash or
similar, hovering over that check-in shows a tooltip with details about
the type of artifact the hash refers to and allows you to click to copy
the hash to the clipboard.

_Graceful Fallback:_ When JavaScript is disabled, these tooltips simply
don’t appear. You can then select and copy the hash using your browser,
make “`fossil info`” queries on those hashes, etc.


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

The default skin includes a “hamburger menu” (&#9776;) which uses
JavaScript to show a simplified version of the Fossil UI site map using
an animated-in dropdown.

_Graceful Fallback:_ Clicking the hamburger menu button with JavaScript
disabled will take you to the `/sitemap` page instead of showing a
simplified version of that page’s content in a drop-down.

_Workaround:_ You can remove this button by [editing the skin][cs]
header.


### <a id="clock"></a>Clock

Some stock Fossil skins include JavaScript-based features such as the
current time of day. The Xekri skin includes this in its header, for
example. A clock feature requires JavaScript not only to get the time
and update inline on the page once a minute, but also so it displays *in
the local time zone.*

Since none of this code provides a necessary Fossil feature, the core
developers are unlikely to try to make these features work better in the
absence of JavaScript.

However, we are willing to study patches to make this better. For
example, the wall clock displays could include the page load time in the
dynamically generated HTML shipped from the remote Fossil server, so
that in the absence of JavaScript, you at least get the page generation
time, expressed in the server’s time zone.
