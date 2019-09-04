# Login Groups

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

-----

*[Back to Administering User Capabilities](./)*
