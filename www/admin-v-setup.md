# The Differences Between the Setup and Admin User Capabilities

Several of the Fossil user capabilities form a clear power hierarchy.
Mathematically speaking:

> *Setup > Admin > Moderator > User > Subscriber > Anonymous > Nobody*
    
This document explains the distinction between the first two. For the
others, see:

* [How Moderation Works](./forum.wiki#moderation)

* [Users vs Subscribers](./alerts.md#uvs)

* [Defense Against Spiders](./antibot.wiki)


## Philosophical Core

The Setup user "owns" the Fossil repository and may delegate a subset of
that power to one or more Admin users.

The Setup user can grant Admin capability and take it away, but Admin
users cannot grant themselves Setup capability, either directly via the
Admin → Users UI page or via any indirect means. (If you discover
indirect means to elevate Admin privilege to Setup, it's a bug, so
please [report it][forum]!)

It is common for the Setup user to have administrative control over the
host system running the Fossil repository, whereas it makes no sense for
Admin users to have that ability. If an Admin-only user had `root`
access on a Linux box running the Fossil instance they are an Admin on,
they could elevate their capability to Setup in several ways. (The
`fossil admin` command, the `fossil sql` command, editing the repository
DB file directly, etc.) Therefore, if you wish to grant someone
Setup-like capability on a Fossil repository but you're unwilling to
give them full control over the host system, you probably want to grant
them Admin capability instead.

Admin power is delegated from Setup. When a Setup user grants Admin
capability, it is an expression of trust in that user's judgement.

Admin-only users must not fight against the policies of the Setup user.
Such a rift would be just cause for the Setup user to strip the Admin
user's capabilities, for the ex-Admin to fork the repository, and for
both to go their separate ways.

A useful rule of thumb here is that Admin users should only change
things that the Setup user has not changed from the stock configuration.
In this way, an Admin-only user can avoid overriding the Setup user's
choices.

This rule is not enforced by the Fossil permission system for a couple
of reasons:

1.  There are too many exceptions to encode in the remaining
    [user capability bits][ucap]. As of this writing, we've already
    assigned meaning to all of the lowercase letters, most of the
    decimal digits, and a few of the uppercase letters. We'd rather not
    resort to punctuation and Unicode to express future extensions to
    the policy choices Fossil offers its power users.

2.  Even if we had enough suitable printable ASCII characters left to
    assign one to every imaginable purpose and policy, we want to keep
    the number of exceptions manageable. Consider the Admin → Settings
    page, which is currently restricted to Setup users only: you might
    imagine breaking this up into several subsets so that some subsets
    are available to non-Setup users, each controlled by a user
    capability bit. Is that a good idea? Maybe, but it should be done
    only after due consideration. It would definitely be wrong to assign
    a user capability bit to *each* setting on that page.

Let's consider a concrete application of this rule: Admin → Skins.
Fossil grants Admin-only users full access to this page so that the
Admins can maintain and extend the skin as the repository evolves, not
so Admins can switch the entire skin to another without consulting with
the Setup user first. If, during a forum discussion one of the mere
users notices a problem with the skin, an Admin-only user should feel
free to correct this without bothering the Setup user.

Another common case is that the Setup user upgrades Fossil on the server
but forgets to merge the upstream skin changes: Admin users are
entrusted to do that work on behalf of the Setup user.


## Capability Groups

We can break up the set of powers the Admin user capability grants into
several groups, then defend each group as a coherent whole.


### Security

While establishing the Fossil repository's security policy is a task for
the Setup user, *maintaining* that policy is something that Fossil
allows a Setup user to delegate to trustworthy users via the Admin user
capability:

*   **Manage users**: The only thing an Admin-only user cannot do on the
    Admin → Users page is grant Setup capability, either to themselves
    or to other users. The intent is that Admin users be able to take
    some of the load of routine user management tasks off the shoulders
    of the Setup user: delete accounts created by spammers, fix email
    alert subscriptions, reset passwords, etc.

*   **Security audit**: The Admin → Security-Audit page runs several
    tests on the Fossil repository's configuration, then reports
    potential problems it found and offers canned solutions. Those
    canned solutions do not do anything that an Admin-user could not do
    via other means. For example, this page's "Take it Private" feature
    can also be done manually via Admin → Users.

*   **Logging**:<a id="log"></a> Admin-only users get to see the various
    Fossil logs in case they need to use them to understand a problem
    they're empowered to solve. An obvious example is a spam attack: the
    Admin might want to find the user's last-used IP, see if they cloned
    the repository, see if they attempted to brute-force an existing
    login before self-registering, etc.

Some security-conscious people might be bothered by the fact that
Admin-only users have these abilities. Think of a large IT organization:
if the CIO hires a [tiger team][tt] to test the company's internal IT
defenses, the line grunts fix the reported problems, not the CIO.


### Administrivia

It is perfectly fine for a Fossil repository to only have Setup users,
no Admin users. The smaller the repository, the more likely the
repository has no Admin-only users. If the Setup user neither needs nor
wants to grant Admin power to others, there is no requirement in Fossil
to do so. [Setup capabilty is a pure superset of Admin capability.][sia]

As the number of users on a Fossil repository grows, the value in
delegating administrivia also grows, because the Setup user typically
has other time sinks they consider more important.

Admin users can take over the following routine tasks on behalf of the
Setup user:

*   **Shunning**: After user management, this is one of the greatest
    powers of an Admin-only user. Fossil grants access to the Admin →
    Shunned page to Admin users rather than reserve it to Setup users
    because one of the primary purposes of [the Fossil shunning
    system](./shunning.wiki) is to clean up after a spammer, and that's
    exactly the sort of adminstrivia we wish to delegate to Admin users.

    Coupled with the Rebuild button on the same page, an Admin user has
    the power to delete the repository's entire
    [blockchain](./blockchain.md)! This makes this feature a pretty good
    razor in deciding whether to grant someone Admin capability: do you
    trust that user to shun Fossil artifacts responsibly?

    Realize that shunning is cooperative in Fossil. As long as there are
    surviving repository clones, an Admin-only user who deletes the
    whole blockchain has merely caused a nuisance. An Admin-only user
    cannot permanently destroy the repository unless the Setup user has
    been so silly as to have no up-to-date clones.

*   **Moderation**: According to the power hierarchy laid out at the top
    of this article, Admins are greater than Moderators, so control over
    what Moderators can do clearly belongs to both Admins and to the
    Setup user(s).

*   **Status**: Although the Fossil `/stat` page is visible to every
    user with Read capability, there are several additional things this
    page gives access to when a user also has the Admin capability:

    *   <p>[Email alerts](./alerts.md) and [backoffice](./backoffice.md)
        status. Admin-only users cannot modify the email alerts setup,
        but they can see some details about its configuration and
        current status.</p>

    *   <p>The `/urllist` page, which is a read-only page showing the
        ways the repository can be accessed and how it has been accessed in
        the past. Logically, this is an extension to logging, [covered
        below](#log).</p>

    *   <p>The Fossil repository SQL schema. This is not particularly
        sensitive information, since you get more or less the same
        information when you clone the repository. It's restricted to
        Admin because it's primarily useful in debugging SQL errors,
        which happen most often when Fossil itself is in flux and the
        schema isn't being automatically updated correctly. That puts
        this squarely into the "administrivia" category.</p>

    *   <p>Web cache status, environment, and logging: more
        administrivia meant to help the Admin debug problems.</p>

*   **Configure search**


### Cosmetics

While the Setup user is responsible for setting up the initial "look" of
a Fossil repository, the Setup user entrusts Admin users with
*maintaining* that look. An Admin-only user therefore has the following
special abilities:

*   Modify the repository skin

*   Create and modify URL aliases

*   Manage the "ad units" feature, if enabled.

*   Adjust the `/timeline` display preferences.

*   Change the "logo" element displayed by some skins.

These capabilities allow an Admin-only user to affect the branding and
possibly even the back-end finances of a project. This is why we began
this document with a philosophical discussion: if you cannot entrust a
user with these powers, you should not grant that user Admin capability.


## Clones and Backups

Keep in mind that Fossil is a *distributed* version control system,
which means that a user known to Fossil might have Setup capability on
one repository but be a mere "user" on one of its clones. The most
common case is that when you clone a repository, even anonymously, you
gain Setup power over the local clone.

The distinctions above therefore are intransitive: they apply only
within a single repository instance.

The exception to this is when the clone is done as a Setup user, since
this also copies the `user` table on the initial clone. A user with
Setup capability can subsequently say [`fossil conf pull all`][fcp] to
update that table and everything else not normally synchronized between
Fossil repositories. In this way, a Setup user can create multiple
interchangeable clones. This is useful not only to guard against rogue
Admin-only users, it is a useful element of a load balancing and
failover system.


[fcp]:   https://fossil-scm.org/fossil/help?cmd=configuration
[forum]: https://fossil-scm.org/forum/
[sia]:   https://fossil-scm.org/fossil/artifact?udc=1&ln=1259-1260&name=0fda31b6683c206a
[tt]:    https://en.wikipedia.org/wiki/Tiger_team#Security
[ucap]:  https://fossil-scm.org/fossil/setup_ucap_list
