# Administering User Capabilities

Fossil includes a powerful [role-based access control system][rbac]
which affects which users have which capabilities within a given
[served][svr] Fossil repository. We call this the capability system, or
“caps” for short.

Fossil stores a user’s caps as an unordered string of ASCII characters,
one capability per, [currently](./impl.md#choices) limited to
[alphanumerics][an]. Caps are case-sensitive: “**A**” and “**a**” are
different user capabilities.

This is a complex topic, so some sub-topics have their own documents:

1.  [Login Groups][lg]
2.  [Implementation Details](./impl.md)
3.  [User Capability Reference](./ref.html)

[an]:   https://en.wikipediAsa.org/wiki/Alphanumeric
[avs]:  ./admin-v-setup.md
[lg]:   ./login-groups.md
[rbac]: https://en.wikipedia.org/wiki/Role-based_access_control


## <a name="ucat"></a>User Categories

Before we explain individual user capabilities and their proper
administration, we want to talk about an oft-overlooked and
misunderstood feature of Fossil: user categories.

Fossil defines four user categories. Two of these apply based on the
user’s login status: **nobody** and **anonymous**. The other two act
like Unix or LDAP user groups: **reader** and **developer**. Because we
use the word “group” for [another purpose][lg] in Fossil, we will
avoid using it that way again in this document. The correct term in
Fossil is “category.”

Fossil user categories give you a way to define capability sets for four
hard-coded situations within the Fossil C source code. Logically
speaking:

> *(developer* &or; *reader)* &ge; *anonymous* &ge; *nobody*

When a user visits a [served Fossil repository][svr] via its web UI,
they initially get the capabilities of the “nobody” user category. This
category would be better named “everybody” because it applies whether
you’re logged in or not.

When a user logs in as “anonymous” via [`/login`](/help?name=/login) they
get all of the “nobody” category’s caps plus those assigned to the
“anonymous” user category. It would be better named “user” because it
affects all logged-in users, not just those logged in via Fossil’s
anonymous user feature.

When a user with either the “reader” ([**u**][u]) or “developer”
([**v**][v]) capability letter logs in, they get their [individual user
caps](#ucap) plus those assigned to this special user category. They
also get those assigned to the “anonymous” and “nobody” categories.

Because “developer” users do not automatically inherit “reader” caps,
it is standard practice to give both letters to your “developer” users:
**uv**. You could instead just assign cap **u** to the “developer”
category.

Fossil shows how these capabilities apply hierarchically in the user
editing screen (Admin → Users → name) with the `[N]` `[A]` `[D]` `[R]`
tags next to each capability check box. If a user gets a capability from
one of the user categories already assigned to it, there is no value in
redundantly assigning that same cap to the user explicitly. For example,
with the default **dei** cap set for the “developer” category, the cap
set **ve** is redundant because **v** grants **dei**, which includes
**e**.

We suggest that you lean heavily on these fixed user categories when
setting up new users. Ideally, your users will group neatly into one of
the predefined categories, but if not, you might be able to shoehorn
them into our fixed scheme. For example, the administrator of a
wiki-only Fossil repo for non-developers could treat the “developer”
user category as if it were called “author,” and a forum-only repo could
treat the same category as if it were called “member.”

There is currently no way to define custom user categories.

[svr]: ../server/


## <a name="ucap"></a>Individual User Capabilities

When one or more users need to be different from the basic capabilities
defined in user categories, you can assign caps to individual users. You
may want to have the [cap reference][ref] open when doing such work.

It is useful at this time to expand on the logical
expression [above](#cat), which covered only the four fixed user categories.
When we bring the individual user capabilities into it, the complete
expression of the way Fossil implements user power becomes:

> *setup* &ge; *admin* &ge; *moderator* &ge; *(developer* &or; *reader)* &ge; *[subscriber]* &ge; *anonymous* &ge; *nobody*

The two additions at the top are clear: [setup is all-powerful][apsu],
and since  admin users have [all capabilities][ref] except for Setup
capability, they are [subordinate only to the setup user(s)][avsp].

The moderator insertion could go anywhere from where it’s shown now down
to above the “anonymous” level, depending on what other caps you give to
your moderators. Also, there is not just one type of moderator: Fossil
has [wiki][l], [ticket][q], and [forum][5] moderators, each
independent of the others. Usually your moderators are fairly
high-status users, with developer capabilities or higher, but Fossil
does allow the creation of low-status moderators.

The placement of “subscriber” in that hierarchy is for the
sort of subscriber who has registered an account on the repository
purely to [receive email alerts and announcements][7]. Users with
additional caps can also be subscribers, but not all users *are* in fact
subscribers, which is why we show it in square brackets.  (See [Users vs
Subscribers](../alerts.md#uvs).)

[apsu]: ./admin-v-setup.md#apsu
[avsp]: ./admin-v-setup.md#philosophy


## <a name="new"></a>New Repository Defaults

Fossil creates one user account in new repos, which is named after your
OS user name [by default](#defuser).

Fossil gives the initial repository user the [all-powerful Setup
capability][apsu].

Users who visit a [served repository][svr] without logging in get the
“nobody” user category’s caps which default to
**[g][g][j][j][o][o][r][r][z][z]**: clone the repo, read the wiki,
check-out files via the web UI, view tickets, and pull version archives.
This default is suited to random passers-by on a typical FOSS project’s
public web site and its code repository.

Users who [prove they are not a bot][bot] by logging in — even if only
as “anonymous” — get the “nobody” capability set plus
**[h][h][m][m][n][n][c][c]**: see internal hyperlinks, append to
existing wiki articles, file new tickets, and comment on existing
tickets. We chose these additional capabilities as those we don’t want
bots to have, but which a typical small FOSS project would be happy to
give anonymous humans visiting the project site.

The “reader” user category is typically assigned to users who want to be
identified within the repository but who primarily have a passive role
in the project. The default capability set on a Fossil repo adds
**[k][k][p][p][t][t][w][w]** caps to those granted by “nobody” and
“anonymous”. This category is not well-named, because the default caps
are all about modifying repository content: edit existing wiki pages,
change one’s own password, create new ticket report formats, and modify
existing tickets. This category would be better named “participant”.

Those in the “developer” category get the “nobody” and “anonymous” cap
sets plus **[d][d][e][e][i][i]**: delete wiki articles and tickets, view
sensitive user material, and check in changes.

[bot]: ../antibot.wiki


## <a name="pvt"></a>Consequences of Taking a Repository Private

When you click Admin → Security-Audit → “Take it private,” one of the
things it does is set the user capabilities for the “nobody” and
“anonymous” user categories to blank, so that users who haven’t logged
in can’t even see your project’s home page, and the option to log in as
“anonymous” isn’t even offered. Until you log in with a user name, all
you see is the repository’s skin and those few UI elements that work
without any user capability checks at all, such as the “Login” link.

Beware: Fossil does not reassign the capabilities these users had to
other users or to the “reader” or “developer” user category! All users
except those with Setup capability will lose all capabilities they
inherited from “nobody” and “anonymous” categories. Setup is the [lone
exception][apsu].

If you will have non-Setup users in your private repo, you should parcel
out some subset of the capability set the “nobody” and “anonymous”
categories had to other categories or to individual users first.


## <a name="read-v-clone"></a>Reading vs. Cloning

Fossil has two capabilities that are often confused:
[**Read**](./ref.html#o) and [**Clone**](./ref.html#g).

The **Read** capability has nothing to do with reading data from a local
repository, because [caps affect Fossil’s web interfaces
only](#webonly). Once you’ve cloned a remote repository to your local
machine, you can do any reading you want on that repository irrespective
of whether your local user within that repo has <b>Read</b> capability.
The repo clone is completely under your user’s power at that point,
affected only by OS file permissions and such. If you need to prevent
that, you want to deny **Clone** capability instead.

Withholding the **Read** capability has a different effect: it
prevents a web client from viewing [embedded
documentation][edoc], using [the file
browser](/help?name=/dir), and pulling file content via the
[`/artifact`](/help?name=/artifact), [`/file`](/help?name=/file), and
[`/raw`](/help?name=/raw) URLs.
It is is common to withhold **Read** capability from low-status visitors
on private or semi-private repos to prevent them from pulling individual
elements of the repo over the web one at a time, as someone may do when
denied the bulk **Clone** capability.

[edoc]: ../embeddeddoc.wiki


## <a name="defuser"></a>Default User Name

By default, Fossil assumes your OS user account name is the same as the
one you use in any Fossil repository. It is the [default for a new
repository](#new), though you can override this with [the `--admin-user`
option][auo]. Fossil has other ways of overriding this in other contexts
such as the `name@` syntax in clone URLs.

It’s simplest to stick with the default; a mismatch can cause problems.
For example, if you clone someone else’s repo anonymously, turn off
autosync, and make check-ins to that repository, they will be assigned
to your OS user name by default. If you later get a login on the remote
repository under a different name and sync your repo with it, your
earlier “private” check-ins will get synced to the remote under your OS
user name!

When such problems occur, you can amend the check-in to hide the
incorrect name from Fossil reports, but the original values remain in
the repository [forever][shun]. It is [difficult enough][fos] to fix
such problems automatically during sync that we are unlikely to ever do
so.

[auo]:  /help?name=new
[fos]:  ./impl.md#filter
[shun]: ../shunning.wiki



## <a name="utclone"></a>Cloning the User Table

When cloning over HTTP, the initial user table in the local clone is set
to its “[new state:](#new)” only one user with Setup capability, named
after either  your OS user account, per the default above, or after the
user given in the clone URL.

There is one exception: if you clone as a named Setup user, you get a
complete copy of the user information. This restriction keeps the user
table private except for the only user allowed to make absolutely
complete clones of a remote repo, such as for failover or backup
purposes. Every other user’s clone is missing this and a few other
items, either for information security or PII privacy reasons.

When cloning with file system paths, `file://` URLs, or over SSH, you
get a complete clone, including the parent repo’s complete user table.

All of the above applies to [login groups][lg] as well.


## <a name="webonly"></a>Caps Affect Web Interfaces Only

User caps only affect Fossil’s [UI pages][wp], remote operations over
`http[s]://` URLs, and [the JSON API][japi].

User caps *do not* affect operations done on a local repo opened via a
`file://` URL or a file system path. This should strike you as sensible:
only local file permissions matter when operating on a local SQLite DB
file. The same is true when working on a clone done over such a path,
except that there are then two sets of file system permission checks:
once to modify the working check-out’s repo clone DB file, then again on
[sync][sync] with the parent DB file. The Fossil capability checks are
effectively defeated because your user has [**Setup**][s] capability on
both sides of the sync.

What may surprise you is that user caps *also do not affect SSH!* When
you make a change to such a repository, the change first goes to the
local clone, where file system permissions are all that matter, but then
upon sync, the situation is effectively the same as when the parent repo
is on the local file system. If you can log into the remote system over
SSH and that user has the necessary file system permissions on that
remote repo DB file, it is the same situation as for `file://` URLs.

All Fossil syncs are done over HTTP, even for `file://` and `ssh://`
URLs:

*   For `ssh://` URLs, Fossil pipes the HTTP conversation through a
    local SSH client to a remote instance of Fossil running the
    [`test-http`](/help?name=test-http) command to recieve the tunneled
    HTTP connection without cap checks. The SSH client defaults to “`ssh
    -e none -T`” on most platforms, except on Windows where it defaults
    to “`plink -ssh -T`”. You can override this with [the `ssh-command`
    setting](/help?name=ssh-command).

*   For `file://` URLs, the “sending” Fossil instance writes its side of
    the HTTP conversation out to a temporary file in the same directory
    as the local repo clone and then calls itself on the “receiving”
    repository to read that same HTTP transcript file back in to apply
    those changes to that repository. Presumably Fossil doesn’t do this
    with a pipe to ease portability to Windows.

Because both mechanisms work on local repos, the checks for capabilities
like [**Read**][o] and [**Write**][i] within the HTTP conversation for
such URLs can never return “false,” because you are the [**Setup**][s]
user on both sides of the conversation. Such checks only have a useful
effect when done over an `http[s]://` URL.


## <a name="pubpg"></a>Public Pages

In Admin → Access, there is an option for giving a list of [globs][glob]
to name URLs which get treated as if the visitor had [the default cap
set](#defcap). For example, you could take the [**Read**][o] capability
away from the “nobody” user category, who has it by default, to prevent
users without logins from pulling down your repository contents one
artifact at a time, yet give those users the ability to read the project
documentation by setting the glob to match your [embedded
documentation][edoc]’s URL root.


## <a name="defcap"></a>Default User Capability Set

In Admin → Access, you can define a default user capability set, which
is used as:

1.  the default caps for users newly created by an Admin or Setup user
2.  the default caps for self-registered users, an option in that same UI
3.  the effective caps for URIs considered [public pages](#pubpg)

This defaults to [**Reader**][u].


<!-- add padding so anchor links always scroll ref’d section to top -->
<div style="height: 75em"></div>

[ref]: ./ref.html

[a]:   ./ref.html#a
[b]:   ./ref.html#b
[c]:   ./ref.html#c
[d]:   ./ref.html#d
[e]:   ./ref.html#e
[f]:   ./ref.html#f
[g]:   ./ref.html#g
[h]:   ./ref.html#h
[i]:   ./ref.html#i
[j]:   ./ref.html#j
[k]:   ./ref.html#k
[l]:   ./ref.html#l
[m]:   ./ref.html#m
[n]:   ./ref.html#n
[o]:   ./ref.html#o
[p]:   ./ref.html#p
[q]:   ./ref.html#q
[r]:   ./ref.html#r
[s]:   ./ref.html#s
[t]:   ./ref.html#t
[u]:   ./ref.html#u
[v]:   ./ref.html#v
[w]:   ./ref.html#w
[x]:   ./ref.html#x
[y]:   ./ref.html#y
[z]:   ./ref.html#z

[2]:   ./ref.html#2
[3]:   ./ref.html#3
[4]:   ./ref.html#4
[5]:   ./ref.html#5
[6]:   ./ref.html#6
[7]:   ./ref.html#7

[glob]: https://en.wikipedia.org/wiki/Glob_(programming)
[japi]: https://docs.google.com/document/d/1fXViveNhDbiXgCuE7QDXQOKeFzf2qNUkBEgiUvoqFN4/view#heading=h.6k0k5plm18p1
[sp]:  ../sync.wiki
[sync]: /help?name=sync
[wp]:  /help#webpages
