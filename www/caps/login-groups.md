# Login Groups

The Admin → Login-Groups UI feature and its corresponding [`login-group`
command][lg] solve a common problem with Fossil: you’ve created multiple
repositories that some subset of users need access to, and you
don’t want to redundantly administer the user credentials for each
repository.


## Restrictions

This feature ties changes to the “`user`” table in one repo to that in
one or more other repos, keyed by the user’s name. The interactions are
non-obvious because although the goal is for the end result to “just
work,” there are practical security and administration matters that
complicate things:

1.  Login group handling only works between joined repositories for the
    subset of users with the same name.

1.  If you’re logged in on one repo in a group, any other repo in that
    group that has a matching user record will accept your valid login.

1.  When you set up a login group between two repos, the user tables
    aren’t merged, so even though you may have users that appear in
    both, they will retain their initial passwords, credentials, and so
    forth.

1.  The same is true after the login group is created: changes you make
    to the user table in one repo in the group only propagate to the
    other repos in the group when you check the “Apply changes to all
    repositories“ box in the “Scope” section of the user edit screen.
    Otherwise, user changes remain local to the repo you made them on.

1.  Login groups only affect [the HTTP interfaces][wo]. Contrast things
    like `ssh://` clones, where unless you [go out of your way][sh] to
    force them to run over one of the HTTP interfaces that pays
    attention to Fossil’s RBAC system, login groups aren’t consulted.


## Interactions

These restrictions combine in subtle and interesting ways. Examples:

*   **#1 and #2**: If you are logged into repo C as “charlie” and then
    try to visit joined repo A where “charlie” doesn’t exist, your valid
    login on C won’t get you into A.

*   **#2 and #3**: If “alice” exists in both of these same repos,
    logging in on A gets her into C, but if she has different user
    capabilities on each from the time before the two repos joined the
    login group, her caps on A don’t apply to C, nor vice versa.

    Let us say F is a forum-only repo, and W is a wiki-only repo, and
    that Alice has forum-posting rights on F and wiki-editing rights on
    W. If both repos are joined by a login group, Alice can log in on F
    and then access W without logging in on it separately, but she
    cannot then post a forum message on W even though she could on F.

*   **#3 and #4**: If you change the caps for user “alice” on one repo
    in a group and tell Fossil to apply the changes to all repos in the
    group, the new caps will *overwrite* those on the other repos, not
    merge with them.

    To extend the practical example from the prior point, let us say you
    wish to grant Alice the “write unversioned” capability on both F and
    W. If you check that single user cap box on F plus the “apply to
    all” option, then “Apply Changes,” she will end up with forum +
    unversioned caps on repo W, losing her wiki-editing caps in the
    process.

    If you want user caps to differ on each repo, you must administer
    them separately even if there is a common subset of caps between all
    repos in the group for that user. Remember: selecting the “apply to
    all” box calls for an overwrite operation, not a merge.

*   **#4 and #1**: If you make a change to an existing user “bob” in
    repo B and select the “apply to all” option, it will only affect
    other repos in the group that have a user “bob” configured.

    But, if you are instead creating user “bob” for the first time and
    select that option, that user *will* be created in all repos.  The
    same is true of user deletion: that destructive action will
    propagate through the group if you request it.

*   **#5 and #1**: If you have a user “daisy” on both repos A and B in a
    login group, logging in over the web to A doesn’t let you push
    changes into B over SSH. Without the workaround linked above, SSH
    only pays attention to the operating system’s user authentication
    system, not Fossil’s.

    Inversely, if Daisy successfully logs in over SSH to repo B, she
    gains no access to any of the other repos in that group. She needs
    at least one valid login over HTTP to one of the group’s repos.


## Discussion

The end result of all of this is that you can have a subset of users
with credentials only on repo A, a different subset only on B, and a
third subset common to both. The only thing selecting which case applies
is restriction #1 above.

Login groups have names. A repo can be in only one of these named login
groups at a time.

Trust in login groups is transitive within a single server. Consider
this sequence:

    $ cd /path/to/A/checkout
    $ fossil login-group join --name G ~/museum/B.fossil
    $ cd /path/to/C/checkout
    $ fossil login-group join ~/museum/B.fossil

That creates login group G joining repo A to B, then joins C to B.
Although we didn’t explicitly tie C to A, a successful login on C gets
you into both A and B, within the restrictions set out above.

Changes are transitive in the same way, provided you check that “apply
to all” box on the user edit screen.

[lg]: /help?cmd=login-group
[sh]: ../server/any/http-over-ssh.md
[wo]: ./index.md#webonly

-----

*[Back to Administering User Capabilities](./)*
