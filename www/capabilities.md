# Administering User Capabilities

Fossil includes a powerful [role-based access control system][rbac]
which affects which users have which capabilities within a given
[served][svr] Fossil repository. We call this the capability system, or
“caps” for short.

Fossil stores a user’s capabilities as an unordered string of ASCII
characters, one capability per, limited to [alphanumerics][an]. Caps are
case-sensitive: “**A**” and “**a**” are different capabilities. We
explain how we came to assign each character [below](#impl).

[an]:   https://en.wikipediAsa.org/wiki/Alphanumeric
[avs]:  ./admin-v-setup.md
[rbac]: https://en.wikipedia.org/wiki/Role-based_access_control
[sync]: /help?cmd=sync


## <a name="ucat"></a>User Categories

Before we explain individual user capabilities and their proper
administration, we want to talk about an oft-overlooked and
misunderstood feature of Fossil: user categories.

Fossil defines four user categories. Two of these apply based on the
user’s login status: **nobody** and **anonymous**. The other two act
like Unix or LDAP user groups: **reader** and **developer**. Because we
use the word “group” for [another purpose](#group) in Fossil, we will
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

When a user logs in as “anonymous” via [`/login`](/help?cmd=/login) they
get all of the “nobody” category’s caps plus those assigned to the
“anonymous” user category. It would be better named “user” because it
affects all logged-in users, not just those logged in via Fossil’s
anonymous user feature.

When a user with either the “reader” ([**u**](#u)) or “developer”
([**v**](#v)) capability letter logs in, they get their [individual user
caps](#ucap) plus those assigned to this special user category. They
also get those assigned to the “anonymous” and “nobody” categories.

Because “developer” users do not automatically inherit “reader” caps,
it is standard practice to give both letters to your “developer” users:
**uv**.

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
them into our fixed scheme. For example, the administrator of a repo
that’s mainly used as a wiki or forum for non-developers could treat the
“developer” user category as if it were called “author”.

There is currently no way to define custom user categories.

[svr]: ./server/


## <a name="ucap"></a>Individual User Capabilities

When one or more users need to be different from the basic capabilities
defined in user categories, you can assign caps to individual users. For
the most part, you want to simply read the [reference material
below](#ref) when doing such work.

However, it is useful at this time to expand on the logical
expression [above](#cat), which covered only the four fixed user categories.
When we bring the individual user capabilities into it, the complete
expression of the way Fossil implements user power becomes:

> *setup* &ge; *admin* &ge; *moderator* &ge; *(developer* &or; *reader)* &ge; *[subscriber]* &ge; *anonymous* &ge; *nobody*

The two additions at the top are clear: [setup is all-powerful][apsu],
and admin users are [subordinate to the setup user(s)](#a). Both are
superior to all other users.

The moderator insertion could go anywhere from where it’s shown now down
to above the “anonymous” level, depending on what other caps you give to
your moderators. Also, there is not just one type of moderator: Fossil
has [wiki](#l), [ticket](#q), and [forum](#5) moderators, each
independent of the others. Usually your moderators are fairly
high-status users, with developer capabilities or higher, but Fossil
does allow the creation of low-status moderators.

The placement of “subscriber” in that hierarchy is for the
sort of subscriber who has registered an account on the repository
purely to [receive email alerts and announcements](#7). Users with
additional caps can also be subscribers, but not all users *are* in fact
subscribers, which is why we show it in square brackets.  (See [Users vs
Subscribers](./alerts.md#uvs).)

[apsu]: ./admin-v-setup.md#apsu


## <a name="new"></a>New Repository Defaults

When you create a new repository, Fossil creates only one user account
named after your OS user name [by default](#defuser).

Fossil gives the initial repository user the [all-powerful Setup
capability][apsu].

Users who visit a [served repository][svr] without logging in get the
“nobody” user category’s caps which default to
**[g](#g)[j](#j)[o](#o)[r](#r)[z](#z)**: clone the repo, read the wiki,
check-out files via the web UI, view tickets, and pull version archives.
The defaults are suited to random passers-by on a typical FOSS project’s
public web site and its code repository.

Users who [prove they are not a bot][bot] by logging in — even if only
as “anonymous” — get the “nobody” capability set plus
**[h](#h)[m](#m)[n](#n)[c](#c)**: see internal hyperlinks, append to
existing wiki articles, file new tickets, and comment on existing
tickets. We chose these additional capabilities as those we don’t want
bots to have, but which a typical small FOSS project would be happy to
give anonymous humans visiting the project site.

The “reader” user category is typically assigned to users who want to be
identified within the repository but who primarily have a passive role
in the project. The default capability set on a Fossil repo adds
**[k](#k)[p](#p)[t](#t)[w](#w)** caps to those granted by “nobody” and
“anonymous”. This category is not well-named, because the default caps
are all about modifying repository content: edit existing wiki pages,
change one’s own password, create new ticket report formats, and modify
existing tickets. This category would be better named “participant”.

Those in the “developer” category get all of the above plus the
**[d](#d)[e](#e)[i](#i)** caps: delete wiki articles and tickets, view
sensitive user material, and check in changes.

[bot]: ./antibot.wiki


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

This is unavoidable because those check-ins are already written durably
to [the local Fossil block chain][bc]. Changing a check-in’s user name
during sync would require rewriting parts of that block chain, which
then means it isn’t actually a “sync” protocol. Either the local and
remote clones would be different or the check-in IDs would change as the
artifacts get rewritten. That in turn means all references to the old
IDs in check-in comments, wiki articles, forum posts, tickets, and more
would break.

When such problems occur, you can amend the check-in to hide the
incorrect name from Fossil reports, but the original values remain in
the repository [forever][shun].

This does mean that anyone with check-in rights on your repository can
impersonate any Fossil user in those check-ins. They check in their work
under any name they like locally, then upon sync, those names are
transferred as-is to the remote repository. Be careful who you give
check-in rights to!

[auo]:  /help?cmd=new
[bc]:   ./blockchain.md
[shun]: ./shunning.wiki



## <a name="group"></a>Login Groups

The Admin → Login-Groups UI feature and its corresponding [`login-group`
command][lg] solve a common problem with Fossil: you’ve created multiple
repositories that some set of users all need access to, those users all
have the same access level on all of these shared repositories, and you
don’t want to redundantly configure the user set for each repository.

This feature ties changes to the “`user`” table in one repo to that in
one or more other repos. With this configured, you get a new choice on
the user edit screen, offering to make changes specific to the one
repository only or to apply it to all others in the login group as well.

A user can log into one repo in a login group only if that user has an
entry in that repo’s user table. That is, setting up a login group
doesn’t automatically transfer all user accounts from the joined repo to
the joining repo. Only when a user exists by name in both repos will
that user be able to share credentials across the repos.

Login groups can have names, allowing one “master” repo to host multiple
subsets of its users to other repos.

Trust in login groups is transitive within a single server. If repo C
joined repo B and repo B joined A, changes in C’s user table affect both
A and B, if you tell Fossil that the change applies to all repos in the
login group.

[lg]: /help?cmd=login-group


## <a name="utclone"></a>Cloning the User Table

When cloning over HTTP, the initial user table in the local clone is set
to its “[new state:](#new)” only one user with Setup capability, named
after either [your OS user account](#defuser) or after the user given in
the clone URL.

There is one exception: if you clone as a named Setup user, you get a
complete copy of the user information. This restriction keeps the user
table private except for the only user allowed to make absolutely
complete clones of a remote repo, such as for failover or backup
purposes. Every other user’s clone is missing this and a few other
items, either for information security or PII privacy reasons.

When cloning with file system paths, `file://` URLs, or over SSH, you
get a complete clone, including the parent repo’s complete user table.

All of the above applies to [login groups](#group) as well.


## <a name="webonly"></a>Caps Affect Web Interfaces Only

User caps only affect Fossil’s [UI pages][wp], remote operations over
`http[s]://` URLs, and [the JSON API][japi].

User caps *do not* affect operations done on a local repo opened via a
`file://` URL or a file system path. This should strike you as sensible:
only local file permissions matter when operating on a local SQLite DB
file. The same is true when working on a clone done over such a path,
except that there are then two sets of file system permission checks:
once to modify the working check-out’s repo clone DB file, then again on
[sync][sync] with the parent DB file.

What may surprise you is that user caps *also do not affect SSH!* When
you make a change to such a repository, the change first goes to the
local clone, where file system permissions are all that matter, but then
upon sync, the situation is effectively the same as when the parent repo
is on the local file system. If you can log into the remote system over
SSH and that user has the necessary file system permissions on that
remote repo DB file, your user is effectively the [all-powerful Setup
user][apsu] on both sides of the SSH connection.

Fossil reuses the HTTP-based [sync protocol][sp] in both cases above,
tunnelling HTTP through an OS pipe or through SSH (FIXME?), but all of
the user cap checks in Fossil are on its web interfaces only.

TODO: Why then can I not `/xfer` my local repo contents to a remote repo
without logging in?

[japi]: https://docs.google.com/document/d/1fXViveNhDbiXgCuE7QDXQOKeFzf2qNUkBEgiUvoqFN4/view#heading=h.6k0k5plm18p1
[sp]:  ./sync.wiki
[wp]:  /help#webpages


## <a name="ref"></a>Capability Reference

This section documents each currently-defined user capability character
in more detail than the brief summary on the [user capability “key”
page](/setup_ucap_list). Each entry begins with the capability letter
used in the Fossil user editor followed by the C code’s name for that
cap within the `FossilUserPerms` object.

The [mnemonics][mn] given here vary from obviously-correct to *post
facto* rationalizations to the outright fanciful. To [some
extent](#choices), this is unavoidable.

*   <a name="a"></a>**a (Admin)** — Admin users have *all* of the capabilities
    below except for [setup](#s). See [Admin vs. Setup][avs] for a more
    nuanced discussion.  Mnemonic: **a**dministrate.

*   <a name="b"></a>**b (Attach)** — Add attachments to wiki articles or tickets.
    Mnemonics: **b**ind, **b**utton, **b**ond, or **b**olt.

*   <a name="c"></a>**c (ApndTkt)** — Append comments to existing tickets.
    Mnemonic: **c**omment.

*   <a name="d"></a>**d (Delete)** — Delete wiki articles or tickets. Mnemonic:
    **d**elete.

*   <a name="e"></a>**e (RdAddr)** — View [personal identifying information][pii]
    (PII) about other users such as email addresses. Mnemonics: show
    **e**mail addresses; or **E**urope, home of [GDPR][gdpr].

*   <a name="f"></a>**f (NewWiki)** — Create new wiki articles. Mnemonic:
    **f**ast, English translation of the Hawaiian word [*wiki*][wnh].

*   <a name="g"></a>**g (Clone)** — Clone the repository. Note that this is
    distinct from [check-out capability, **o**](#o). Mnemonic: **g**et.

*   <a name="h"></a>**h (Hyperlink)** — Get hyperlinks in generated HTML which link
    you to other parts of the repository. This capability exists and is
    disabled by default for the “nobody” category to [prevent bots from
    wandering around aimlessly][bot] in the site’s hyperlink web,
    chewing up server resources to little good purpose. Mnemonic:
    **h**yperlink.

*   <a name="i"></a>**i (Write)** — Check changes into the repository. Note that
    a lack of this capability does not prevent you from checking changes
    into your local clone, only from syncing those changes up to the
    parent repo, and then [only over HTTP](#webonly). Granting this
    capability also grants **o (Read)**.  Mnemonic: check **i**n
    changes.

*   <a name="j"></a>**j (RdWiki)** — View wiki articles. Mnemonic: in**j**est
    page content.  (All right, you critics, you do better, then.)

*   <a name="k"></a>**k (WrWiki)** — Edit wiki articles. Granting this
    capability also grants **j (RdWiki)** and **m (ApndWiki)**, but it
    does *not* grant **f (NewWiki)**! Mnemonic: **k**ontribute.

*   <a name="l"></a>**l (ModWiki)** — Moderate [wiki article appends](#m). Appends
    do not get saved permamently to the receiving repo’s block chain
    until some user (one with this cap or [Setup cap](#s)) approves it.
    Mnemonic: a**l**low.

*   <a name="m"></a>**m (ApndWiki)** — Append content to existing wiki articles.
    Mmnemonics: a**m**end or **m**odify.

*   <a name="n"></a>**n (NewTkt)** — File new tickets. Mnemonic: **n**ew ticket.

*   <a name="o"></a>**o (Read)** — Read repository content from a remote
    Fossil instance over HTTP. This capability has nothing to do with
    reading data from a local repository, because [caps affect Fossil’s
    web interfaces only](#webonly). Once you’ve cloned a remote
    repository to your local machine, you can do any reading you want on
    that repository irrespective of whether your user has **o**
    capability; the repo clone is completely under your user’s power at
    that point, affectted only by OS file permissions and such. (To
    prevent cloning, see [**g**](#g).)

    It is common to withhold this capability from low-status visitors to
    prevent them from viewing [embedded documentation][edoc], seeing
    [the file browser][du], and pulling file content via the
    [`/artifact`][au], [`/file`][fu], and [`/raw`][ru] URLs.

    Mnemonic: check **o**ut remote repo contents.

*   <a name="p"></a>**p (Password)** — Change one’s own password.  Mnemonic:
    **p**assword.

*   <a name="q"></a>**q (ModTkt)** — Moderate tickets: comments appended to
    tickets can be deleted by users with this capability. Mnemonic:
    **q**uash noise commentary.

*   <a name="r"></a>**r (RdTkt)** — View existing tickets. Mnemonic: **r**ead
    tickets.

*   <a name="s"></a>**s (Setup)** — The [all-powerful Setup user][apsu].
    Mnemonics: **s**etup or **s**uperuser.

*   <a name="t"></a>**t (TktFmt)** — Create new ticket report formats. Note that
    although this allows the user to provide SQL code to be run in the
    server’s context, and this capability is given to the untrusted
    “anonymous” user category by default, this is a safe capability to
    give to users because it is internally restricted to read-only
    queries on the tickets table only. (This restriction is done with a
    SQLite authorization hook, not by any method so weak as SQL text
    filtering.) Mnemonic: new **t**icket report.

*   <a name="u"></a>**u** — Inherit all capabilities of the “reader”
    user category; does not have a dedicated flag internally within
    Fossil.  Mnemonic: **u**ser, per [naming suggestion
    above](#cat).

*   <a name="v"></a>**v** — Inheheit all capabilities of the “developer”
    user category; does not have a dedicated flag internally within
    Fossil.  Mnemonic: de**v**eloper.

*   <a name="w"></a>**w (WrTkt)** — Edit existing tickets. Granting this
    capability also grants **r (RdTkt)**, **c (ApndTkt)**, and **n
    (NewTkt)**. Mnemonic: **w**rite to ticket.

*   <a name="x"></a>**x (Private)** — Push or pull [private branches][pb].
    Mnemonic: e**x**clusivity; “x” connotes unknown material in many
    Western languages due to its [traditional use in mathematics][lgrd]

*   <a name="y"></a>**y (WrUnver)** — Push [unversioned content][uv]. Mnemonic:
    **y**ield, [sense 4][ywik]: “hand over.”

*   <a name="z"></a>**z (Zip)** — Pull archives of particular repository
    versions via [`/zip`][zu], [`/tarball`][tbu], and [`/sqlar`][sau]
    URLs. This is an expensive capability to assign, because creating
    such archives can put a large load on [a Fossil server][svr], which
    you may then need to [manage][load]. Mnemonic: **z**ip file
    download.

*   <a name="2"></a>**2 (RdForum)** — Read [forum posts][for] by other users.
    Mnemonic: from thee **2** me.

*   <a name="3"></a>**3 (WrForum)** — Create new forum threads, reply to threads
    created by others, and edit one’s own posts. New posts are held for
    [moderation][fmod], and they are marked to prevent them from being
    included in clone and sync operations. Granting this capability also
    grants **2 (RdForum)**. Mnemonic: post for **3**
    audiences: me, [the mods](#5), and [the Man][man].

*   <a name="4"></a>**4 (WrTForum)** — Extends cap [**3**](#3) so that
    forum updates bypass the [moderation and private artifact
    restrictions][fmod]. Granting this capability also grants **2
    (RdForum)**. Mnemonic: post 4 immediate release.

*   <a name="5"></a>**5 (ModForum)** — [Moderate][fmod] forum posts. Note that this
    capabilitty does not automatically grant [**4**](#4), so it is
    possible to have a user that can create a new post via capability
    [**3**](#3) and then approve that post immediately themselves with
    *this* capability! Granting this capability also grants caps **4
    (WrTForum)** and **2 (RdForum)**. Mnemonic: “May I have **5**
    seconds of your time, honored Gatekeeper?”

*   <a name="6"></a>**6 (AdminForum)** — Users with this capability see a checkbox on
    un-moderated forum posts labeled “Trust user X so that future posts
    by user X do not require moderation.” Checking that box and then
    clicking the moderator-only “Approve” button on that post grants
    capability [**4**](#4) to that post’s author. There is currently no
    UI for a user with capability **6** to remove trust from a user once
    it is granted. Granting this capability also grants cap **5
    (ModForum)** and those it in turn grants.
    Mnemonic: “I’m six of hitting Approve on your posts!”

*   <a name="7"></a>**7 (EmailAlert)** — Sign up for [email alerts][ale]. Mnemonic:
    [Seven can wait][scw], I’ve got email to read now.

*   <a name="A"></a>**A (Announce)** — Send email announcements to users
    [signed up to receive them](#7).  Mnemonic: **a**nnounce.

*   <a name="D"></a>**D (Debug)** — Enable debugging features. Mnemonic:
    **d**ebug.


[ale]:  ./alerts.md
[au]:   /help?cmd=/artifact
[du]:   /help?cmd=/dir
[edoc]: ./embeddeddoc.wiki
[fmod]: ./forum.wiki#moderation
[for]:  ./forum.wiki
[fu]:   /help?cmd=/file
[gdpr]: https://en.wikipedia.org/wiki/General_Data_Protection_Regulation
[lgrd]: https://en.wikipedia.org/wiki/La_Géométrie#The_text
[load]: ./loadmgmt.md
[man]:  https://en.wikipedia.org/wiki/The_Man
[pb]:   ./private.wiki
[pii]:  https://en.wikipedia.org/wiki/Personal_data
[sau]:  /help?cmd=/sqlar
[tbu]:  /help?cmd=/tarball
[ru]:   /help?cmd=/raw
[scw]:  https://en.wikipedia.org/wiki/Heaven_Can_Wait
[uv]:   ./unvers.wiki
[wnh]:  https://en.wikipedia.org/wiki/History_of_wikis#WikiWikiWeb,_the_first_wiki
[ywik]: https://en.wiktionary.org/wiki/yield
[zu]:   /help?cmd=/zip


## <a name="impl"></a>Implementation Details

### <a name="choices"></a>Capability Letter Choices

We assigned user capability characters using only lowercase ASCII
letters at first, so those are the most important within Fossil: they
control the functions most core to Fossil’s operation. Once we used up
most of the lowercase letters, we started using uppercase, and then
during the development of the [forum feature][for] we assigned most of
the decimal numerals.  Eventually, we might have to start using
punctuation. We expect to run out of reasons to define new caps before
we’re forced to switch to Unicode, though the possibilities for mnemonic
assignments with emoji are intriguing. <span style="vertical-align:
bottom">😉</span>

The existing caps are usually mnemonic, especially among the
earliest and therefore most central assignments, made when we still had
lots of letters to choose from.  There is still hope for good future
mnemonic assignments among the uppercase letters, which are mostly still
unused.


### <a name="bitfield"></a>Why Not Bitfields?

When Fossil is deciding whether a user has a given capability, it simply
searches the cap string for a given character. This is slower than
checking bits in a bitfield, but it’s fast enough in the context where
it runs: at the front end of an HTTP request handler, where the
nanosecond differences in such implementation details are completely
swamped by the millisecond scale ping time of that repo’s network
connection, followed by the requires I/O to satisfy the request. A
[`strchr()` call](https://en.cppreference.com/w/c/string/byte/strchr) is
plenty fast in that context.

[mn]:   https://en.wikipedia.org/wiki/Mnemonic
