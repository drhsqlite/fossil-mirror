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
   cryptography. Each block contains a cryptographic hash of the previous
   block, a timestamp, and transaction data (generally represented as a Merkle tree)."


By that partial definition, Fossil is indeed a blockchain. The blocks
are Fossil’s ["manifest" artifacts](./fileformat.wiki#manifest). Each
manifest has a cryptographically-strong [SHA-1] or [SHA-3] hash linking it to
one or more “parent” blocks. The manifest also contains a timestamp and
the transactional data needed to express a commit to the repository. If
you traverse the Fossil repository from the tips of its [DAG] to the
root by following the parent hashes in each manifest, you will then have
a Merkle tree. Point-for-point, Fossil follows that definition.

Every change in Fossil starts by adding one or more manifests to
the repository.

[bcwp]:  https://en.wikipedia.org/wiki/Blockchain
[DAG]:   https://en.wikipedia.org/wiki/Directed_acyclic_graph
[SHA-1]: https://en.wikipedia.org/wiki/SHA-1
[SHA-3]: https://en.wikipedia.org/wiki/SHA-3



## Cryptocurrency

Because blockchain technology was first popularized as Bitcoin, many
people associate the term with cryptocurrency.  Fossil has nothing to do
with cryptocurrency, so a claim that “Fossil is a blockchain” may run up
against problems due to conflation with cryptocurrency.

Cryptocurrency has several features and requirements that Fossil doesn’t
provide, either because it doesn’t need them or because we haven’t
gotten around to creating the feature. Whether these are essential to
the definition of “blockchain” and thus make Fossil “not a blockchain”
is for you to decide.

1.  **Signatures.** Blocks in a cryptocurrency have to be signed by the
    *prior* owner of each block in order to transfer the money to the
    new holder, else the new recipient could claim to have received any
    amount of money they want by editing the face value of the currency
    block. The chain of signatures also lets us verify that each block
    is transferred only once, solving the double-spending problem. These
    are both types of forgery, but they’re distinct sorts: changing a
    US $20 bill to $100 is different from simply making more $20 bills
    that look sufficiently like the original.

    This chain of signatures prevents both types of forgery, and it is a
    second type of link between the blocks, separate from the “hash
    chain” that applies an ordering to the blocks. (This distinction of
    terms comes from [_Blockchain: Simple Explanation_][bse].)

    Fossil has an off-by-default feature to call out to an external copy
    of PGP or GPG to sign commit manifests before inserting them into
    the repository, but it’s rarely used, and even when it is used,
    Fossil doesn’t currently verify those signatures in any way.

    Even if Fossil someday gets a built-in commit signature feature, and
    even if this new feature enforces a rule that rejects commits that
    don’t include a verifiable signature, Fossil will still not provide
    the sort of cross-block transfer signatures needed by
    cryptocurrencies. Fossil commit signatures simply attest that the
    new commit was created by some verifiable person while preventing
    that attestation and the block it attests to from being changed.  (A
    failure in this feature would be analogous to the first type of
    forgery above: changing the “face value” of a commit.) As long as I
    retain control over my private commit signing key, no one can take
    one of my commits and change its contents.

    There is no need in Fossil for cross-commit sign-overs, because
    there is no useful analog to double-spending fraud in Fossil.

    The lack of commit signing in the default Fossil configuration means
    forgery of commits is possible by anyone with commit capability. If
    that is an essential element to your notion of “blockchain,” and you
    wish to have some of the same guarantees from Fossil as you get from
    other types of blockchains, then you should enable its [clearsign
    feature][cs], coupled with a server-side [“after receive” hook][arh]
    to reject commits if they aren’t signed.

    Fossil’s chain of hashes prevents modification of existing commits
    as long as the receiving Fossil server is secure. Even if you manage
    to execute a [preimage attack][prei] on the hash algorthm — SHA3-256
    by default in the current version of Fossil — our sync protocol will
    prevent the modification from being accepted into the repository. To
    modify an existing commit, an attacker would have to attack the
    remote host itself somehow, not its repository data structures.
    Strong signatures are only needed to prevent *new* commits from
    being forged at the tips of the DAG, and to avoid the need to trust
    the remote Fossil server quite so heavily.

    If you’re wondering why Fossil currently lacks built-in commit
    signing and verification, and why its current commit signing feature
    is not enabled by default, it is because Fossil is not itself a
    [PKI], and there is no way for regular users of Fossil to link it to
    a PKI, since doing so would likely result in an unwanted [PII]
    disclosure.  There is no email address in a Fossil commit manifest
    that you could use to query one of the public PGP keyservers, for
    example. It therefore becomes a local policy matter as to whether
    you even *want* to have signatures, because they’re not without
    their downsides.

2.  **Longest-Chain Rule.** Cryptocurrencies generally need some way to
    distinguish which blocks are legitimate and which not.

    There is the proof-of-work aspect of this, which has no useful
    application to Fossil, so we can ignore that.

    The other aspect of this does have applicability to Fossil is the
    notion (as in Bitcoin) that the linear chain with the greatest
    cumulative work-time is the legitimate chain. Everything else is
    considered an “orphan” block and is ignored by the software. The
    closest we can come to that notion in Fossil is the default “trunk”
    branch, but there’s nothing in Fossil that delegitimizes other
    branches just because they’re shorter, nor is there any way in
    Fossil to score the amount of work that went into a commit. Indeed,
    [forks and branches][fb] are *valuable and desirable* things in
    Fossil.

3.  **Work Contests.** Cryptocurrencies prevent forgery by setting up
    some sort of contest that ensures that new coins can come into
    existence only by doing some difficult work task. This “mining”
    activity results in a coin that took considerable work to create,
    which thus has economic value by being a) difficult to re-create,
    and b) resistant to [debasement][dboc].

    Fossil repositories are most often used to store the work product of
    individuals, rather than cryptocoin mining machines. There is
    generally no contest in trying to produce the most commits.
    Incentives to commit to the repository come from outside of Fossil;
    they are not inherent to its nature, as with cryptocurrencies.
    Moreover, there is no useful sense in which we could say that one
    commit “re-creates” another. Commits are generally products of
    individual human intellect, thus necessarily unique in all but
    trivial cases. Thus the entire basis of copyright law.

This much is certain: Fossil is definitely not a cryptocurrency.

[arh]:  https://fossil-scm.org/fossil/doc/trunk/www/hooks.md
[bse]:  https://www.researchgate.net/publication/311572122_What_is_Blockchain_a_Gentle_Introduction
[cs]:   https://fossil-scm.org/home/help?cmd=clearsign
[dboc]: https://en.wikipedia.org/wiki/Debasement
[fb]:   https://fossil-scm.org/home/doc/trunk/www/branching.wiki
[PII]:  https://en.wikipedia.org/wiki/Personal_data
[PKI]:  https://en.wikipedia.org/wiki/Public_key_infrastructure
[prei]: https://en.wikipedia.org/wiki/Preimage_attack



## Distributed Ledgers

Cryptocurrencies are a type of [distributed ledger technology][dlt]. If
we can convince ourselves that Fossil is also a type of distributed
ledger, then we might think of Fossil as a peer technology, thus also a
type of blockchain.

A key tenet of DLT is that records be unmodifiable after they’re
committed to the ledger, which matches quite well with Fossil’s design
and everyday use cases. Fossil puts up multiple barriers to prevent
modification of existing records and injection of incorrect records.

Yet, Fossil also has [purge] and [shunning][shun]. Doesn’t that mean
Fossil cannot be a distributed ledger?

These features remove commits from the repository. If you want a
currency analogy, they are ways to burn a paper bill or to melt a [fiat
coin][fc] down to slag. In a cryptocurrency, you can erase your “wallet”
file, effectively destroying money in a similar way. You can’t use these
features of Fossil to forge new commits or forge a modification to an
existing commit.

What if we removed those features from Fossil, creating an append-only
variant? Is it a DLT then? Arguably still not, because [today’s Fossil
is an AP-mode system][fapm] in the [CAP theorem][cap] sense, which means
there can be no guaranteed consensus on the content of the ledger at any
given time. If you had an AP-mode accounts receivable system, it could
have different bottom-line totals at different sites, because you’ve
cast away “C” to get AP-mode operation.

What are the prospects for CA-mode or CP-mode Fossil? [We don’t want
CA-mode Fossil, but CP-mode could be useful.][fapm] Until the latter
exists, this author believes Fossil is not a distributed ledger in a
technologically defensible sense.

The most common technologies answering to the label “blockchain” are all
DLTs, so if Fossil is not a DLT, then it is not a blockchain in that
sense.

[fapm]:   ./cap-theorem.md
[cap]:    https://en.wikipedia.org/wiki/CAP_theorem
[dlt]:    https://en.wikipedia.org/wiki/Distributed_ledger
[DVCS]:   https://en.wikipedia.org/wiki/Distributed_version_control
[fc]:     https://en.wikipedia.org/wiki/Fiat_money
[purge]:  /help?cmd=purge
[shun]:   ./shunning.wiki


## Distributed Partial Consensus

If we can’t get DLT, can we at least get some kind of distributed
consensus at the level of individual Fossil’s commits?

Many blockchain based technologies have this property: given some
element of the blockchain, you can make certain proofs that it either is
a legitimate part of the whole blockchain, or it is not.

Unfortunately, this author doesn’t see a way to do that with Fossil.
Given only one “block” in Fossil’s putative “blockchain” — a commit, in
Fossil terminology — all you can prove is whether it is internally
consistent, not corrupt. That then points you at the parent(s) of that
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

A future version of Fossil could instead provide consensus [in the CAP
sense][fapm]. For instance, you could say that if a quorum of servers
all have a given commit, it “belongs.” Fossil’s strong hashing tech
would mean that querying whether a given commit is part of the
“blockchain” would be as simple as going down the list of servers and
sending it an HTTP GET `/info` query for the artifact ID, returning
“Yes” once you get enough HTTP 200 status codes back. All of this is
hypothetical, because Fossil doesn’t do this today.

[AGI]:  https://en.wikipedia.org/wiki/Artificial_general_intelligence
[rcks]: https://fossil-scm.org/home/help?cmd=repo-cksum


# Conclusion

This author believes it is technologically indefensible to call Fossil a
“blockchain” in any sense likely to be understood by a majority of those
you’re communicating with.

Within a certain narrow scope, you can defend this usage, but if you do
that, you’ve failed any goal that requires clear communication: it
doesn’t work to use a term in a nonstandard way just because you can
defend it.  The people you’re communicating your ideas to must have the
same concept of the terms you use.


What term should you use instead? A blockchain is a type of [Merkle
tree][mt], named after [its inventor][drrm]. You could also call it by
the more generic term “hash tree.” That Fossil certainly is.

Fossil is a technological peer to many common types of blockchain
technology. There is a lot of overlap in concepts and implementation
details, but when speaking of what most people understand as
“blockchain,” Fossil is not that.

[drrm]: https://en.wikipedia.org/wiki/Ralph_Merkle
[mt]:   https://en.wikipedia.org/wiki/Merkle_tree
