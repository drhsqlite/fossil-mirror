# Fossil Push Policy

"Fossil Push Policy" or "FDD" is a proposed mechanism to help project
administrators help enforce project development policies by restricting
the kinds of changes that can be pushed up into a community server.

## Background

The default behavior of Fossil is that any developer who has push
privileges on a repository can push any content.  Project-specific
policy choices, such as "don't land unapproved changes on trunk"
or "don't reopen a closed ticket" are enforced adminstratively, not
by Fossil itself.  Fossil maintains a detailed audit trail, so that policy
violations can be traced back to their source, and so that
violators can be duely reprimanded.  Fossil also provides mechanisms
so that unapproved changes can be excised from critical branches without
deleting history.  But by default, Fossil
does not attempt to disallow unauthorized changes from occurring in the
first place.

Nothing can prevent a developer from making non-conforming and/or
unauthorized change in a private client-side clone of a Fossil repository,
as the client-side repository is under the complete control of the
developer who owns it.
Any automatic policy enforcement must happen on the common repository
when the developer attempts to push.

FDD is *not* a security mechanism.  FDD is not intended to allow
untrusted individuals to push to a common repository.  FDD does not
guarantee that no undesirable changes ever get pushed.  Rather, FDD
is designed as an adminstrative aid and an automatic mechanism to prevent
accidents or misunderstandings.

## Example Use Cases

Here are examples of the kinds of pushes that FDD is designed to prevent
for unauthorized users:

  *   Do not allow check-ins on trunk (or some other
      important branch).

  *   Do not allow changes to specific files within
      the project.

  *   Do not allow changes to specific wiki pages.

  *   Do not allow changes to tickets unless the ticket is in specific
      state.

  *   Do not allow new branches unless the branch name
      matches a specific GLOB, LIKE, or REGEXP pattern.

  *   Do not allow tags to be added to a check-in created by a different
      developer.

The foregoing is not an exhaustive list of the kinds of behavior that FDD
is suppose to detect and prevent.  It is just a representative sample of
the kinds of changes FDD is intended to detect and disallow.

Rules can be written such that they apply only to specific developers,
groups or classes of developers, or to everyone other than administrators.
Administrators can also be subject to FDD rules, though they have the
option to override.

## Mechanism

FDD is a set of rules contained in a single configuration setting.
The rules consist of Boolean expression and error message pairs.
At the end of each push request received by the server, all boolean
expressions are evaluated.  If any boolean is true, then the push
fails with its associated error message.

If a push fails, the client repository must be repaired (for example
by adding tags to change the branch of check-in or to cancel a ticket
change) to fix the problem.  Then the push can be retried.  Fixing
FPP violations is a serious inconvenience to the developer who violated the
policy, since the violations must be fixed before *any* future push
is allowed.  This inconvenience is intended to motivate the developer
to better follow the rules in the future.

## Client Side Warnings

FDD rules are downloaded on a "fossil clone" and on "fossil config pull fdd".
When rules exist on the client side, then certain client-side operations
(such as "fossil commit")
are also evaluated for rule violations, and appropriate warnings are issued.
However, for client-side operations, rules can always be overridden, since
the owner of the client-side clone is always administrator for their private
clone.  Early rule check-in helps prevent the client-side repository
from becoming unpushable due to typos or other accidents that would have
caused a rule violation.
