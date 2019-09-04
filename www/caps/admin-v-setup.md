# Differences Between Setup and Admin User

This document explains the distinction between [Setup users][su] and
[Admin users][au]. For other information about use types, see:

* [Administering User Capabilities](./)
* [How Moderation Works](./forum.wiki#moderation)
* [Users vs Subscribers](./alerts.md#uvs)
* [Defense Against Spiders](./antibot.wiki)

[au]: ./ref.md#a
[su]: ./ref.md#s


## <a name="philosophy"></a>Philosophical Core

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
`fossil user` command, the `fossil sql` command, editing the repository
DB file directly, etc.) Therefore, if you wish to grant someone
Setup-like capability on a Fossil repository but you're unwilling to
give them a login on the host system, you probably want to grant
them Admin capability instead.

Admin power is delegated from Setup. When a Setup user grants Admin
capability, it is an expression of trust in that user's judgement.

Admin-only users must not fight against the policies of the Setup user.
Such a rift would be just cause for the Setup user to strip the Admin
user's capabilities. This may then create a fork in the project’s
development effort as the ex-Admin takes their clone and stands it up
elsewhere, so they may become that fork’s Setup user.

A useful rule of thumb here is that Admin users should only change
things that the Setup user has not changed from the stock configuration.
In this way, an Admin-only user can avoid overriding the Setup user's
choices.

You can also look at the role of Admin from the other direction, up
through the [user power hierarchy][ucap] rather than down from Setup. An
Admin user is usually a “super-developer” role, given full control over
the repository’s managed content: versioned artifacts in [the block
chain][bc], [unversioned content][uv], forum posts, wiki articles,
tickets, etc.

We’ll explore these distinctions in the rest of this document.

[bc]:   ./blockchain.md
[ucap]: ./index.md#ucap
[uv]:   ./unvers.wiki


## <a name="binary"></a>No Granularity

Fossil doesn’t make any distinction between these two user types beyond
this binary choice: Setup or Admin.

A few features of Fossil are broken down so that only part of the
feature is accessible to Admin, with the rest left only to Setup users,
but for the most part each feature affected by this distinction is
either Admin + Setup or Setup-only.

We could add more capability letters to break down individual
sub-features, but we’d run out of ASCII alphanumerics pretty quickly,
and we might even run out of ASCII punctuation and symbols. Then would
we need to shift to Unicode?

Consider the Admin → Settings page, which is currently restricted to
Setup users only: you might imagine breaking this up into several
subsets so that some settings can be changed by Admin users.  Is that a
good idea? Maybe, but it should be done only after due consideration. It
would definitely be wrong to assign a user capability bit to *each*
setting on that page.

Now consider the opposite sort of case, Admin → Skins.  Fossil grants
Admin users full access to this page so that the Admins can maintain and
extend the skin as the repository evolves, not so Admins can switch the
entire skin to another without consulting with the Setup user first. How
would Fossil decide, using user capabilities only, which skin changes
the Admin user is allowed to do, and which must be left to Setup? Do we
assign a separate capability letter to each step in `/setup_skin`? Do we
assign one more each to the five sections of a skin? (Header, Footer,
CSS, JavaScript, and Details.) It quickly becomes unmanageable.



## <a name="capgroups"></a>Capability Groups

We can break up the set of powers the Admin user capability grants into
several groups, then defend each group as a coherent whole.


### <a name="security"></a>Security

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
    via other means, so this page offers the Admin-only user no more
    power than they otherwise had. For example, this page's "Take it
    Private" feature can also be done manually via Admin → Users. This
    page is a convenience, not a grant of new power to the Admin-only
    user.

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


### <a name="administrivia"></a>Administrivia

It is perfectly fine for a Fossil repository to only have Setup users,
no Admin users. The smaller the repository, the more likely the
repository has no Admin-only users. If the Setup user neither needs nor
wants to grant Admin power to others, there is no requirement in Fossil
to do so. [Setup capability is a pure superset of Admin capability.][sia]

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
    exactly the sort of administrivia we wish to delegate to Admin users.

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

    *   <p>[Email alerts][ale] and [backoffice](./backoffice.md)
        status. Admin-only users cannot modify the email alerts setup,
        but they can see some details about its configuration and
        current status.</p>

    *   <p>The `/urllist` page, which is a read-only page showing the
        ways the repository can be accessed and how it has been accessed
        in the past. Logically, this is an extension to logging,
        [covered above](#log).</p>

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

[ale]: ./alerts.md


### <a name="cosmetics"></a>Cosmetics

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


## <a name="clones"></a>Clones and Backups

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


## <a name="apsu"></a>The All-Powerful Setup User

Setup users get [every user capability](./ref.html) of Fossil except for
[**WrUnver**](./ref.html#y). (That lone hold-out is [by design][snoy].)

In addition, Setup users can use every feature of the Fossil UI. If Fossil can do a
thing, a Setup user on that repo can make Fossil do it.

Setup users can do many things that Admin users cannot:

*   Use all of the Admin UI features
*   See record IDs (RIDs) on screens that show them
*   See the MIME type of attachments on [`/ainfo` pages](/help?cmd=/ainfo)
*   See a remote repo’s HTTP [cache status](/help?cmd=/cachestat)
    and [pull cache entries](/help?cmd=/cacheget)
*   Edit a Setup user’s account!

The “Admin” feature of Fossil UI is so-named because Admin users can use
about half of its functions, but only Setup can use these pages:

*   **Access**: This page falls under the [Security](#security)
    category above, but like Configuration, it's generally something set
    up once and never touched, so only Setup users should change it.

*   **Configuration**: This page nominally falls
    under [Cosmetics](#cosmetics) above, but it's such a core part of the Fossil
    configuration — something every Setup user is expected to fully
    specify on initial repository setup — that we have trouble
    justifying any case where an Admin-only user would have good cause
    to modify any of it. This page is generally set up once and then
    never touched again.

*   **Email-Server**: This is an experimental SMTP server feature which
    is currently unused in Fossil. Should we get it working, it will
    likely remain Setup-only, since it will likely be used as a
    replacement for the platform’s default SMTP server, a powerful
    position for a piece of software to take.
  
*   **Login-Group**: [Login groups][lg] allow one Fossil repository to
    delegate user access to another. Since an Admin-only user on one
    repo might not have such access to another repo on the same host
    system, this must be a Setup-only task.

*   **Notification**: This is the main UI for setting up integration
    with a platform’s SMTP service, for use in sending out [email
    notifications][ale]. Because this screen can set commands to execute
    on the host, and because finishing the configuration requires a
    login on the Fossil host system, it is not appropriate to give Admin
    users access to it.

*   **Settings**: The [repository settings][rs] available via Admin →
    Settings have too wide a range of power to allow modification by
    Admin-only users:

    *   <p><b>Harmless</b>: Admin-only users on a repository may well
        have checkin rights on the repository, so the fact that
        versionable settings like `crlf-glob` can also be set at the
        repository level seems like a thing we might want to allow
        Admin-only users the ability to change. Since Fossil currently
        has no way to allow only some settings to be changed by
        Admin-only users and some not, we can't just show these harmless
        settings to Admin-only users.</p>

    *   <p><b>Low-Risk</b>: The <tt>admin-log</tt> setting controls
        whether the Fossil admin log is generated. Since we've <a
        href="#log">already decided</a> that Admin-only users can see
        this log, it seems fine that the Admin users can choose whether
        this log gets generated in the first place.</p>

        <p>There's a small risk that a rogue Admin user could disable
        the log before doing something evil that the log would capture,
        so ideally, we'd want to restrict changing this setting from 1
        to 0 to Setup only while allowing Admin-only users to change it
        from 0 to 1. Fossil doesn't currently allow that.</p>

    *   <p><b>Risky</b>: The <tt>https-login</tt> setting falls under
        the "Security" section above, but it should probably never be
        adjusted by Admin-only users. Sites that want it on will never
        want it to be disabled without a very good reason.</p>

        <p>There is also an inverse risk: if the site has a front-end
        HTTPS proxy that uses HTTP to communicate over localhost to
        Fossil, enabling this setting will create an infinite redirect
        loop! (Ask me how I know.)</p>

    *   <p><b>Dangerous</b>: The <tt>email-send-command</tt> setting
        could allow a rogue Admin to run arbitrary commands on the host
        system, unless it's prevented via some kind of host-specific
        restriction.  (chroot, jails, SELinux, VMs, etc.) Since it makes
        no sense to trust Admin-only users with <tt>root</tt> level
        access on the host system, we almost certainly don't want to
        allow them to change such settings.</p>

*   **SQL**: The Admin → SQL feature allows the Setup user to enter raw
    SQL queries against the Fossil repository via Fossil UI. This not
    only allows arbitrary ability to modify the repository blockchain
    and its backing data tables, it can probably also be used to damage
    the host such as via `PRAGMA temp_store = FILE`.

*   **Tickets**: This section allows input of aribtrary TH1 code that
    runs on the server, affecting the way the Fossil ticketing system
    works. The justification in the **TH1** section below therefore
    applies.

*   **TH1**: The [TH1 language][TH1] is quite restricted relative to the
    Tcl language it descends from, so this author does not believe there
    is a way to damage the Fossil repository or its host via the Admin →
    TH1 feature, which allows execution of arbitrary TH1 code within the
    repository's execution context. Nevertheless, interpreters are a
    well-known source of security problems, so it seems best to restrict
    this feature to Setup-only users as long as we lack a good reason
    for Admin-only users to have access to it.

*   **Transfers**: This is for setting up TH1 hooks on various actions,
    so the justification in the **TH1** section above applies.

*   **Wiki**: These are mainly cosmetic and usability settings. We might
    open this up to Admin users in the future.

Just remember, [user caps affect Fossil’s web interfaces only][webo].  A
user is a Setup user by default on their local clone of a repo, and
Fossil’s ability to protect itself against malicious (or even simply
incorrect) pushes is limited. Someone with clone and push capability on
your repo could clone it, modify their local repo, and then push the
changes back to your repo. Be careful who you give that combination of
capabilities to!

When you run [`fossil ui`][fui], you are the Setup user on that repo
through that UI instance, regardless of the capability set defined in
the repo’s user table. This is true even if you cloned a remote repo
where you do not have Setup caps. This is why `ui` always binds to
`localhost` without needing the `--localhost` flag: in this mode, anyone
who can connect to that repo’s web UI has full power over that repo.

[fcp]:   https://fossil-scm.org/fossil/help?cmd=configuration
[forum]: https://fossil-scm.org/forum/
[fui]:   /help?cmd=ui
[lg]:    ./login-groups.md
[rs]:    https://www.fossil-scm.org/index.html/doc/trunk/www/settings.wiki
[sia]:   https://fossil-scm.org/fossil/artifact?udc=1&ln=1259-1260&name=0fda31b6683c206a
[snoy]:  https://fossil-scm.org/forum/forumpost/00e1c4ecff
[tt]:    https://en.wikipedia.org/wiki/Tiger_team#Security
