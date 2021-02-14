# Fossil and the CAP Theorem

[The CAP theorem][cap] is a fundamental mathematical proof about
distributed systems.  A software system can no more get around it than a
physical system can get past *c*, the [speed of light][sol] constant.

Fossil is a distributed system, so it can be useful to think about it in
terms of the CAP theorem. We won’t discuss the theorem itself or how you
reason using its results here. For that, we recommend [this article][tut].

[cap]: https://en.wikipedia.org/wiki/CAP_theorem
[sol]: https://en.wikipedia.org/wiki/Speed_of_light
[tut]: https://www.ibm.com/cloud/learn/cap-theorem


<a id="ap"></a>
## Fossil Is an AP-Mode System

As with all common [DVCSes][dvcs], Fossil is an AP-mode system, meaning
that your local clone isn’t necessarily consistent with all other clones
(C), but the system is always available for use (A) and
partition-tolerant (P). This is what allows you to turn off Fossil’s
autosync mode, go off-network, and continue working with Fossil, even
though only a single node (your local repo clone) is accessible at the
time.

You may consider that going back online restores “C”, because upon sync,
you’re now consistent with the repo you cloned from. But, if another
user has gone offline in the meantime, and they’ve made commits to their
disconnected repo, *you* aren’t consistent with *them.* Besides which,
if another user commits to the central repo, that doesn’t push the
change down to you automatically: even if all users of a Fossil system
are online at the same instant, and they’re all using autosync, Fossil
doesn’t guarantee consistency across the network.

There’s no getting around the CAP theorem!

[dvcs]: https://en.wikipedia.org/wiki/Distributed_version_control


<a id="ca"></a>
## CA-Mode Fossil

What would it mean to redesign Fossil to be CA-mode?

It means we get a system that is always consistent (C) and available (A)
as long as there are no partitions (P).

That’s basically [CVS] and [Subversion][svn]: you can only continue
working with the repository itself as long as your connection to the central repo server functions.

It’s rather trivial to talk about single-point-of-failure systems like
CVS or Subversion as
CA-mode. Another common example used this way is a classical RDBMS, but
aren’t we here to talk about distributed systems? What’s a good example
of a *distributed* CA-mode system?

A better example is [Kafka], which in its default configuration assumes
it being run on a corporate LAN in a single data center, so network
partitions are exceedingly rare. It therefore sacrifices partition
tolerance to get the advantages of CA-mode operation. In its particular application of
this mode, a
message isn’t “committed” until all running brokers have a copy of it,
at which point the message becomes visible to the client(s). In that
way, all clients always see the same message store as long as all of the
Kafka servers are up and communicating.

How would that work in Fossil terms?

If there is only one central server and I clone it on my local laptop,
then CA mode means I can only commit if the remote Fossil is available,
so in that sense, it devolves to the old CVS model.

What if there are three clones? Perhaps there is a central server *A*,
the clone *B* on my laptop, and the clone *C* on your laptop. Doesn’t CA
mode now mean that my commit on *B* doesn’t exist after I commit it to
the central repo *A* until you, my coworker, *also* pull down the copy
of that commit to your laptop *C*, validating the commit through the
network?

That’s one way to design the system, but another way would be to scope
the system to only talk about proper *servers*, not about the clients.
In that model, a CA-mode Fossil alternative might require 2+ servers to
be running for proper replication. When I make a commit, if all of the
configured servers aren’t online, I can’t commit. This is basically CVS
with replication, but without any useful amount of failover.

[CVS]:   https://en.wikipedia.org/wiki/Concurrent_Versions_System
[Kafka]: https://engineering.linkedin.com/kafka/intra-cluster-replication-apache-kafka
[svn]:   https://en.wikipedia.org/wiki/Apache_Subversion


<a id="cp"></a>
## CP-Mode Fossil

What if we modify our CA-mode system above with “warm spares”?  We can
say that commits must go to all of the spares as well as the active
servers, but a loss of one active server requires that one warm spare
come into active state, and all of the clients learn that the spare is
now considered “active.” At this point, you have a CP-mode system, not a
CA-mode system, because it’s now partition-tolerant (P) but it becomes
unavailable when there aren’t enough active servers or warm
spares to promote to active status.

CP is your classical [BFT] style distributed consensus system, where the
system is available only if the client can contact a *majority* of the
servers. This is a formalization of the warm spare concept above: with
*N* server nodes, you need at least ⌊*N* / 2⌋ + 1 of them to be online
for a commit to succeed.

Many distributed database systems run in CP mode because consistency (C) and
partition-tolerance (P) is a useful combination. What you lose is
always-available (A) operation: with a suitably bad partition, the
system goes down for users on the small side of that partition.

An optional CP mode for Fossil would be attractive in some ways since in
some sense Fossil is a distributed DBMS, but in practical terms, it
means Fossil would then not be a [DVCS] in the most useful sense, being
that you could work while your client is disconnected from the remote
Fossil it cloned from.

A fraught question is whether the non-server Fossil clones count as
“nodes” in this sense.

If they do count, then if there are only two systems, the central server
and the clone on my laptop, then it stands to reason from the formula
above that I can only commit if the central server is available. In that
scheme, a CP-mode Fossil is basically like CVS.

But what happens if my company hires a coworker to help me with the
project, and this person makes their own clone of the central repo? The
equation says I still need 2 nodes to be available for a commit, so if
my new coworker goes off-network, that doesn’t affect whether I can make
commits. Likewise, if I go off-network, my coworker can make commits to
the central server.

But what happens if the central server goes down? The equation says we
still have 2 nodes, so we should be able to commit, right? Sure, but
only if my laptop and communicate directly to my coworker’s laptop! If
it can’t, that’s also a network partition, so *N=1* on both sides in
that case. The implication is that for a true CP-mode Fossil, we’d need
some kind of peer-to-peer networking layer so that our laptops can
accept commits from the other, so that when the central server comes
online, one of us can send the results up to it to get it caught up.

But doesn’t that then mean there is no security? How does [Fossil’s RBAC
system][caps] work if peer-to-peer commits are allowed?

You can instead reconceptualize the system as “node” meaning only server
nodes, so that client-only systems don’t count. This allows you to have
an RBAC system again.

With just one central server, ⌊1/2⌋+1=1, so you get CVS-like behavior:
if the server’s up, you can commit.

If you set up 2 servers for redundancy, both must be up for commits to
be allowed, since otherwise you could end up with half the commits going
to the server on one side of a network partition, half going to the
other, and no way to arbitrate among the two once the partition is
lifted.

(Today’s AP-mode Fossil has this capability, but the necessary cost is
“C”, consistency! Once again, you can’t get around the CAP theorem.)

3 servers is more sensible: any client that can see at least 2 of them
can commit.

Will there ever be a CP-mode Fossil? This author doubts it, but as I’ve
shown, it would be useful in contexts where you’d rather have a
guarantee of consistency than availability.

[BFT]:    https://en.wikipedia.org/wiki/Byzantine_fault
[caps]:   ./caps/
