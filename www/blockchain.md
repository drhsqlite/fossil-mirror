# Is Fossil A Blockchain?

The Fossil version control system shares a lot of similarities with
other blockchain based technologies, but it also differs from the more common
sorts of blockchains. This document will discuss the term’s
applicability, so you can decide whether applying the term to Fossil
makes sense to you.


## The Dictionary Argument

The [Wikipedia definition of "blockchain"][bcwp] begins:

>
  "A blockchain…is a growing list of records, called blocks, which are linked using
   cryptography… Each block contains a cryptographic hash of the previous
   block, a timestamp, and transaction data (generally represented as a Merkle tree)."

Point-for-point, Fossil follows this partial definition.
The blocks
are Fossil’s ["manifest" artifacts](./fileformat.wiki#manifest). Each
manifest has a cryptographically-strong [SHA-1] or [SHA-3] hash linking it to
one or more “parent” blocks. The manifest also contains a timestamp and
the transactional data needed to express a commit to the repository.
To traverse the Fossil repository from the tips of its [DAG] to the
root by following the parent hashes in each manifest is to traverse
a Merkle tree.
Every change in Fossil starts by adding one or more manifests to
the repository, extending this Merkle tree.

[bcwp]:  https://en.wikipedia.org/wiki/Blockchain
[DAG]:   https://en.wikipedia.org/wiki/Directed_acyclic_graph
[SHA-1]: https://en.wikipedia.org/wiki/SHA-1
[SHA-3]: https://en.wikipedia.org/wiki/SHA-3



<a id="currency"></a>
## Cryptocurrency

Because blockchain technology was first popularized as Bitcoin, many
people associate the term with cryptocurrency.  Fossil has nothing to do
with cryptocurrency, so a claim that “Fossil is a blockchain” may fail
to communicate the speaker’s concepts clearly due to conflation with
cryptocurrency.

Cryptocurrency has several features and requirements that Fossil doesn’t
provide, either because it doesn’t need them or because we haven’t
gotten around to creating the feature. Whether these are essential to
the definition of “blockchain” and thus disqualify Fossil as a blockchain
is for you to decide.

Cryptocurrencies must prevent three separate types of fraud to be useful:

*   **Type 1** is modification of existing currency. To draw an analogy
    to paper money, we wish to prevent someone from using green and
    black markers to draw extra zeroes on a US $10 bill so that it
    claims to be a $100 bill.

*   **Type 2** is creation of new fraudulent currency that will pass
    in commerce.  To extend our analogy, it is the creation of new
    US $10 bills. There are two sub-types to this fraud. In terms of
    our analogy, they are:

    *  **Type 2a**: copying an existing legitimate $10 bill<br><br>

    *  **Type 2b**: printing a new $10 bill that is unlike an existing
       legitimate one, yet which will still pass in commerce

*   **Type 3** is double-spending existing legitimate cryptocurrency.
    There is no analogy in paper money due to its physical form; it is a
    problem unique to digital currency due to its infinitely-copyable
    nature.

How does all of this compare to Fossil?

1.  <a id="signatures"></a>**Signatures.** Cryptocurrencies use a chain
    of [digital signatures][dsig] to prevent Type 1 and Type 3 frauds. This
    chain forms an additional link between the blocks, separate from the
    hash chain that applies an ordering and lookup scheme to the blocks.
    [_Blockchain: Simple Explanation_][bse] explains this “hash chain”
    vs. “block chain” distinction in more detail.

    These signatures prevent modification of the face value of each
    transaction (Type 1 fraud) by ensuring that only the one signing a
    new block has the private signing key that could change an issued
    block after the fact.

    The fact that these signatures are also *chained* prevents Type
    3 frauds by making the *prior* owner of a block sign it over to
    the new owner. To avoid an O(n²) auditing problem as a result,
    cryptocurrencies add a separate chain of hashes to make checking
    for double-spending quick and easy.

    Fossil has [a disabled-by-default feature][cs] to call out to an
    external copy of [PGP] or [GPG] to sign commit manifests before
    inserting them into the repository. You can couple that with
    a server-side [after-receive hook][arh] to reject unsigned commits.

    Although there are several distinctions you can draw between the way
    Fossil’s commit signing scheme works and the way block signing works
    in cryptocurrencies, only one is of material interest for our
    purposes here: Fossil commit signatures apply only to a single
    commit. Fossil does not sign one commit over to the next “owner” of
    that commit in the way that a blockchain-based cryptocurrency must
    when transferring currency from one user to another, beacuse there
    is no useful analog to the double-spending problem in Fossil.  The
    closest you can come to this is double-insert of commits into the
    blockchain, which we’ll address shortly.

    What Fossil commit signatures actually do is provide in-tree forgery
    prevention, both Type 1 and Type 2. You cannot modify existing
    commits (Type 1 forgery) because you do not have the original
    committer’s private signing key, and you cannot forge new commits
    attesting to come from some other trusted committer (Type 2) because
    you don’t have any of their private signing keys, either.
    Cryptocurrencies use the work problem to prevent Type 2
    forgeries, but the application of that to Fossil is a matter we get
    to [later](#work).

    Although you have complete control over the contents of your local
    Fossil repository clone, you cannot perform Type 1 forgery on its
    contents short of executing a [preimage attack][prei] on the hash
    algorithm. ([SHA3-256][SHA-3] by default in the current version of
    Fossil.) Even if you could, Fossil’s sync protocol will prevent the
    modification from being pushed into another repository: the remote
    Fossil instance says, “I’ve already got that one, thanks,” and
    ignores the push.  Thus, short of breaking into the remote server
    and modifying the repository in place, you couldn’t make use of
    a preimage attack even if you had that power. Further, that would be an attack on the
    server itself, not on Fossil’s data structures, so while it is
    useful to think through this problem, it is not helpful in answering
    our questions here.

    The Fossil sync protocol’s duplication detection also prevents the closest analog to Type 3
    frauds in Fossil: copying a commit manifest in your local repo clone
    won’t result in a double-commit on sync.

    In the absence of digital signatures, Fossil’s [RBAC system][caps]
    restricts Type 2 forgery to trusted committers. Thus once again
    we’re reduced to an infosec problem, not a data structure design
    question.

    (Inversely, enabling commit clearsigning is a good idea
    if you have committers on your repo whom you don’t trust not to
    commit Type 2 frauds. But let us be clear: your choice of setting
    does not answer the question of whether Fossil is a blockchain.)

    If Fossil signatures prevent Type 1 and Type 2 frauds, you
    may wonder why they are not enabled by default. It is because
    they are defense-in-depth measures, not the minimum sufficient
    measures needed to prevent repository fraud, unlike the equivalent
    protections in a cryptocurrency blockchain. Fossil provides its
    primary protections through other means, so it doesn’t need to
    mandate signatures.

    Also, Fossil is not itself a [PKI], and there is no way for regular
    users of Fossil to link it to a PKI, since doing so would likely
    result in an unwanted [PII] disclosure.  There is no email address
    in a Fossil commit manifest that you could use to query one of the
    public PGP keyservers, for example. It therefore becomes a local
    policy matter as to whether you even *want* to have signatures,
    because they’re not without their downsides.

2.  <a id="work"></a>**Work Contests.** Cryptocurrencies prevent Type 2b forgeries
    by setting up some sort of contest that ensures that new coins can come
    into existence only by doing some difficult work task. This “mining”
    activity results in a coin that took considerable work to create,
    which thus has economic value by being a) difficult to re-create,
    and b) resistant to [debasement][dboc].

    Fossil repositories are most often used to store the work product of
    individuals, rather than cryptocoin mining machines. There is
    generally no contest in trying to produce the most commits. There
    may be an implicit contest to produce the “best” commits, but that
    is a matter of project management, not something that can be
    automatically mediated through objective measures.

    Incentives to commit to the repository come from outside of Fossil;
    they are not inherent to its nature, as with cryptocurrencies.
    Moreover, there is no useful sense in which we could say that one
    commit “re-creates” another. Commits are generally products of
    individual human intellect, thus necessarily unique in all but
    trivial cases. This is foundational to copyright law.

3.  <a id="lcr"></a>**Longest Chain Rule.** Cryptocurrencies generally
    need some way to distinguish which blocks are legitimate and which
    not.  They do this in part by identifying the linear chain with the
    greatest cumulative [work time](#work) as the legitimate chain. All
    blocks not on that linear chain are considered “orphans” and are
    ignored by the cryptocurrency software.

    Its inverse is sometimes called the “51% attack” because a single
    actor would have to do slightly more work than the entire rest of
    the community using a given cryptocurrency in order for their fork
    of the currency to be considered the legitimate fork. This argument
    soothes concerns that a single bad actor could take over the
    network.

    The closest we can come to that notion in Fossil is the default
    “trunk” branch, but there’s nothing in Fossil that delegitimizes
    other branches just because they’re shorter, nor is there any way in
    Fossil to score the amount of work that went into a commit. Indeed,
    [forks and branches][fb] are *valuable and desirable* things in
    Fossil.

This much is certain: Fossil is definitely not a cryptocurrency. Whether
this makes it “not a blockchain” is a subjective matter.

[arh]:  ./hooks.md
[bse]:  https://www.researchgate.net/publication/311572122_What_is_Blockchain_a_Gentle_Introduction
[caps]: ./caps/
[cs]:   /help?cmd=clearsign
[dboc]: https://en.wikipedia.org/wiki/Debasement
[dsig]: https://en.wikipedia.org/wiki/Digital_signature
[fb]:   ./branching.wiki
[GPG]:  https://gnupg.org/
[PGP]:  https://www.openpgp.org/
[PII]:  https://en.wikipedia.org/wiki/Personal_data
[PKI]:  https://en.wikipedia.org/wiki/Public_key_infrastructure
[pow]:  https://en.wikipedia.org/wiki/Proof_of_work
[prei]: https://en.wikipedia.org/wiki/Preimage_attack



<a id="dlt"></a>
## Distributed Ledgers

Cryptocurrencies are an instance of [distributed ledger technology][dlt]. If
we can convince ourselves that Fossil is also a distributed
ledger, then we might think of Fossil as a peer technology,
having at least some qualifications toward being considered a blockchain.

A key tenet of DLT is that records be unmodifiable after they’re
committed to the ledger, which matches quite well with Fossil’s design
and everyday use cases. Fossil puts up multiple barriers to prevent
modification of existing records and injection of incorrect records.

Yet, Fossil also has [purge] and [shunning][shun]. Doesn’t that mean
Fossil cannot be a distributed ledger?

These features only remove existing commits from the repository. If you want a
currency analogy, they are ways to burn a paper bill or to melt a [fiat
coin][fc] down to slag. In a cryptocurrency, you can erase your “wallet”
file, effectively destroying money in a similar way. These features
do not permit forgery of either type described above: you can’t use them
to change the value of existing commits (Type 1) or add new commits to
the repository (Type 2).

What if we removed those features from Fossil, creating an append-only
Fossil variant? Is it a DLT then? Arguably still not, because [today’s Fossil
is an AP-mode system][ctap], which means
there can be no guaranteed consensus on the content of the ledger at any
given time. An AP-mode accounts receivable system would allow
different bottom-line totals at different sites, because you’ve
cast away “C” to get AP-mode operation. (See the prior link or
[Wikipedia’s article on the CAP theorem][cap] if you aren’t following
this terminology.)

By the same token, you cannot guarantee that the command
“`fossil info tip`” gives the same result everywhere. You would need to
recast Fossil as a CA or CP-mode system to solve that.
(Everyone not
partitioned away from the majority of the network at any rate, in the CP
case.)

What are the prospects for CA-mode or CP-mode Fossil? [We don’t want
CA-mode Fossil][ctca], but [CP-mode could be useful][ctcp]. Until the latter
exists, this author believes Fossil is not a distributed ledger in a
technologically defensible sense.

The most common technologies answering to the label “blockchain” are all
DLTs, so if Fossil is not a DLT, then it is not a blockchain in that
sense.

[ctap]:   ./cap-theorem.md#ap
[ctca]:   ./cap-theorem.md#ca
[ctcp]:   ./cap-theorem.md#cp
[cap]:    https://en.wikipedia.org/wiki/CAP_theorem
[dlt]:    https://en.wikipedia.org/wiki/Distributed_ledger
[DVCS]:   https://en.wikipedia.org/wiki/Distributed_version_control
[fc]:     https://en.wikipedia.org/wiki/Fiat_money
[purge]:  /help?cmd=purge
[shun]:   ./shunning.wiki


<a id="dpc"></a>
## Distributed Partial Consensus

If we can’t get DLT, can we at least get some kind of distributed
consensus at the level of individual Fossil’s commits?

Many blockchain based technologies have this property: given some
element of the blockchain, you can make certain proofs that it either is
a legitimate part of the whole blockchain, or it is not.

Unfortunately, this author doesn’t see a way to do that with Fossil.
Given only one “block” in Fossil’s putative “blockchain” — a commit, in
Fossil terminology — all you can prove is whether it is internally
consistent, that it is not corrupt. That then points you at the parent(s) of that
commit, which you can repeat the exercise on, back to the root of the
DAG. This is what the enabled-by-default [`repo-cksum` setting][rcks]
does.

If cryptocurrencies worked this way, you wouldn’t be able to prove that
a given cryptocoin was legitimate without repeating the proof-of-work
calculations for the entire cryptocurrency scheme! Instead, you only
need to check a certain number of signatures and proofs-of-work in order
to be reasonably certain that you are looking at a legitimate section of
the whole blockchain.

What would it even mean to prove that a given Fossil commit “*belongs*”
to the repository you’ve extracted it from? For a software project,
isn’t that tantamount to automatic code review, where the server would
be able to reliably accept or reject a commit based solely on its
content? That sounds nice, but this author believes we’ll need to invent
[AGI] first.

A better method to provide distributed consensus for Fossil would be to
rely on the *natural* intelligence of its users: that is, distributed
commit signing, so that a commit is accepted into the blockchain only
once some number of users countersign it. This amounts to a code review
feature, which Fossil doesn’t currently have.

Solving that problem basically requires solving the [PKI] problem first,
since you can’t verify the proofs of these signatures if you can’t first
prove that the provided signatures belong to people you trust. This is a
notoriously hard problem in its own right.

A future version of Fossil could instead provide [consensus in the CAP
sense][ctcp]. For instance, you could say that if a quorum of servers
all have a given commit, it “belongs.” Fossil’s strong hashing tech
would mean that querying whether a given commit is part of the
“blockchain” would be as simple as going down the list of servers and
sending each an HTTP GET `/info` query for the artifact ID, concluding
that the commit is legitimate once you get enough HTTP 200 status codes back. All of this is
hypothetical, because Fossil doesn’t do this today.

[AGI]:  https://en.wikipedia.org/wiki/Artificial_general_intelligence
[rcks]: /help?cmd=repo-cksum



<a id="anon"></a>
## Anonymity

Many blockchain based technologies go to extraordinary lengths to
allow anonymous use of their service.

As typically configured, Fossil does not: commits synced between servers
always at least have a user name associated with them, which the remote
system must accept through its [RBAC system][caps]. That system can run
without having the user’s email address, but it’s needed if [email
alerts][alert] are enabled on the server. The remote server logs the IP
address of the commit for security reasons. That coupled with the
timestamp on the commit could sufficiently deanonymize users in many
common situations.

It is possible to configure Fossil so it doesn’t do this:

* You can give [Write capability][capi] to user category “nobody,” so
  that anyone that can reach your server can push commits into its
  repository.

* You could give that capability to user category “anonymous” instead,
  which requires that the user log in with a CAPTCHA, but which doesn’t
  require that the user otherwise identify themselves.

* You could enable [the `self-register` setting][sreg] and choose not to
  enable [commit clear-signing][cs] so that anonymous users could push
  commits into your repository under any name they want.

On the server side, you can also [scrub] the logging that remembers
where each commit came from.

Commit source info isn’t transmitted from the remote server on clone or pull:
the size of the `rcvfrom` table after initial clone is 1, containing
only the remote server’s IP address. On each pull containing new
artifacts, your local `fossil` instance adds another entry to this
table, likely with the same IP address unless the server has moved or
you’re using [multiple remotes][mrep]. This table is far more
interesting on the server side, containing the IP addresses of all
contentful pushes; thus [the `scrub` command][scrub].

Because Fossil doesn’t
remember IP addresses in commit manifests or require commit signing, it
allows at least *pseudonymous* commits. When someone clones a remote
repository, they don’t learn the email address, IP address, or any other
sort of [PII] of prior committers, on purpose.

Some people say that private, permissioned blockchains (as you may
imagine Fossil to be) are inherently problematic by the very reason that
they don’t bake anonymous contribution into their core. The very
existence of an RBAC is a moving piece that can break. Isn’t it better,
the argument goes, to have a system that works even in the face of
anonymous contribution, so that you don’t need an RBAC? Cryptocurrencies
do this, for example: anyone can “mine” a new coin and push it into the
blockchain, and there is no central authority restricting the transfer
of cryptocurrency from one user to another.

We can draw an analogy to encryption, where an algorithm is
considered inherently insecure if it depends on keeping any information
from an attacker other than the key. Encryption schemes that do
otherwise are derided as “security through obscurity.”

You may be wondering what any of this has to do with whether Fossil is a
blockchain, but that is exactly the point: all of this is outside
Fossil’s core hash-chained repository data structure. If you take the
position that you don’t have a “blockchain” unless it allows anonymous
contribution, with any needed restrictions provided only by the very
structure of the managed data, then Fossil does not qualify.

Why do some people care about this distinction? Consider Bitcoin,
wherein an anonymous user cannot spam the blockchain with bogus coins
because its [proof-of-work][pow] protocol allows such coins to be
rejected immediately. There is no equivalent in Fossil: it has no
technology that allows the receiving server to look at the content of a
commit and automatically judge it to be “good.” Fossil relies on its
RBAC system to provide such distinctions: if you have a commit bit, your
commits are *ipso facto* judged “good,” insofar as any human work
product can be so judged by a blob of compiled C code. This takes us
back to the [digital ledger question](#dlt), where we can talk about
what it means to later correct a bad commit that got through the RBAC
check.

We may be willing to accept pseudonymity, rather than full anonymity.
If we configure Fossil as above, either bypassing the RBAC or abandoning
human control over it, scrubbing IP addresses, etc., is it then a public
permissionless blockchain in that sense?

We think not, because there is no [longest chain rule](#lcr) or anything
like it in Fossil.

For a fair model of how a Fossil repository might behave under such
conditions, consider GitHub: here one user can fork another’s repository
and make an arbitrary number of commits to their public fork.  Imagine
this happens 10 times. How does someone come along later and
*automatically* evaluate which of the 11 forks of the code (counting the
original repository among their number) is the “best” one? For a
computer software project, the best we could do to approximate this
devolves to a [software project cost estimation problem][scost]. These
methods are rather questionable in their own right, being mathematical
judgement values on human work products, but even if we accept their
usefulness, then we still cannot say which fork is better based solely
on their scores under these metrics. We may well prefer to use the fork
of a software program that took *less* effort, being smaller, more
self-contained, and with a smaller attack surface.


[alert]: ./alerts.md
[capi]:  ./caps/ref.html#i
[mrep]:  /help?cmd=remote
[scost]: https://en.wikipedia.org/wiki/Software_development_effort_estimation
[scrub]: /help?cmd=scrub
[sreg]:  /help?cmd=self-register


# Conclusion

This author believes it is technologically indefensible to call Fossil a
“blockchain” in any sense likely to be understood by a majority of those
you’re communicating with. Using a term in a nonstandard way just because you can
defend it means you’ve failed any goal that requires clear communication.
The people you’re communicating your ideas to must have the
same concept of the terms you use.

What term should you use instead? Fossil stores a DAG of hash-chained
commits, so an indisputably correct term is a [Merkle tree][mt], named
after [its inventor][drrm].  You could also use the more generic term
“hash tree.”

Fossil is a technological peer to many common sorts of blockchain
technology. There is a lot of overlap in concepts and implementation
details, but when speaking of what most people understand as
“blockchain,” Fossil is not that.

[drrm]: https://en.wikipedia.org/wiki/Ralph_Merkle
[mt]:   https://en.wikipedia.org/wiki/Merkle_tree
