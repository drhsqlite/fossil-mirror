# Is Fossil A Blockchain?

The Fossil version control system shares a lot of similarities with
blockchain based technologies, but it also differs from the more common
sorts of blockchains. This document will discuss the term’s
applicability, so you can decide whether applying the term to Fossil
makes sense to you.


## The Dictionary Argument

[Wikipedia defines "blockchain"][bcwp] in part as

>
  "…a growing list of records, called blocks, which are linked using
   cryptography. Each block contains a cryptographic hash of the previous
   block, a timestamp, and transaction data…"


By that partial definition, Fossil is indeed a blockchain.
The blocks are ["manifests" artifacts](./fileformat.wiki#manifest).
Each manifest has a SHA1 or SHA3 hash of its parent or parents,
a timestamp, and other transactional data.  The repository grows by
adding new manifests onto the list.

Nevertheless, there are many reasons to regard Fossil as *not* a
blockchain.

[bcwp]: https://en.wikipedia.org/wiki/Blockchain



## Cryptocurrency

Because blockchain technology was first popularized as Bitcoin, many
people associate the term with cryptocurrency.  Since Fossil has nothing
to do with cryptocurrency, someone using the term “blockchain” to refer
to Fossil is likely to fail to communicate their ideas clearly.

Cryptocurrency also has unfortunate implications in certain circles, its
anonymity and lack of regulation leading it to become associated with
bad actors. Even if we ignore all of the other criticisms in this
document, our unwillingness to be so associated may be enough of a
reason for us to avoid using it.



## Marketing Capture

The fact that blockchain technology has become a hot marketing buzzword
should affect your choice of whether to use the term “blockchain” to
refer to Fossil. Your choice may well vary based on the audience:

*   **Executive Board:** At the quarterly all-hands meeting, the big
    boss — who just read about blockchains in [PHB] Weekly — asks if
    your development organization “has a blockchain.” With Fossil and a
    suitably narrow definition of the term “blockchain” in mind, you
    could answer “Yes,” except that you know they’re then going to go to
    the shareholders and happily report, “Our development organization
    has been using blockchain technology for years!” You may decide that
    this makes you responsible for a public deception, putting the
    organization at risk of an SEC investigation for making false
    statements.

    Yet if you answer “No,” knowing you’ll be punished for not being on
    top of the latest whiz-bang as the technologically gormless PHB sees
    it, are you advancing the organization’s actual interests? If the
    organization has no actual need for a proper blockchain tech base,
    isn’t it better to just say “Yes” and point at Fossil so you can get
    back to useful work?

*   **Middle Management:** Your project leader asks the same question,
    so you point them at this document, which tells them the truth:
    kinda yes, but mostly no.

*   **Developer Lunch:** A peer asks if you’re doing anything with
    blockchains. Knowing the contents of this document, you decide you
    can’t justify using that term to refer to Fossil at a deep technical
    level, so you admit that you are not.

[PHB]:   https://en.wikipedia.org/wiki/Pointy-haired_Boss


## Distributed Ledgers

Cryptocurrencies are a type of [distributed ledger technology][dlt]. Is
Fossil a distributed ledger?

A key tenet of DLT is that records be unmodifiable after they’re
committed to the ledger, which matches quite well with Fossil’s design
and everyday use cases.

Yet, Fossil also has [purge] and [shunning][shun]. Doesn’t that mean
Fossil cannot be a distributed ledger?

What if you removed those features from Fossil, creating an append-only
variant? Is it a DLT then? Arguably still not, because [today’s Fossil
is an AP-mode system][fapm] in the [CAP theorem][cap] sense, which means
there can be no guaranteed consensus on the content of the ledger at any
given time. If you had an AP-mode accounts receivable system, it could
have different bottom-line totals at different sites, because you’ve
cast away “C” to get AP-mode operation.

What are the prospects for CA-mode or CP-mode Fossil? [We don’t want
CA-mode Fossil, but CP-mode could be useful.][fapm] Until the latter
exists, this author believes Fossil is not a distributed ledger in a
technologically defensible sense. If you restrict your definition’s
scope to cover only the most common uses of “blockchain,” which are all
DLTs, that means Fossil is not a blockchain.

[fapm]:   ./cap-theorem.md
[cap]:    https://en.wikipedia.org/wiki/CAP_theorem
[dlt]:    https://en.wikipedia.org/wiki/Distributed_ledger
[DVCS]:   https://en.wikipedia.org/wiki/Distributed_version_control
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
calculations for the entire cryptocurrency scheme!

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

Even with all of the above solved, you’d still have another problem:
Fossil currently has no way to do partial cloning of a repository. The
only way to remotely extract individual “blocks” — commits — from a
remote repository is to make `/artifact`, `/info`, or `/raw` queries to
its HTTP interface. For Fossil to be a true blockchain, we’d want a way
to send around as little as one commit which could be individually
verified as being “part of the blockchain” using only intra-block
consistency checks.

[AGI]:  https://en.wikipedia.org/wiki/Artificial_general_intelligence
[PKI]:  https://en.wikipedia.org/wiki/Public_key_infrastructure
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
tree][mt], also called a hash tree, and Fossil is certainly that.

Fossil and “blockchain” are technological peers. They are related
technologies, but neither is a subset or instance of the other in any
useful sense.

[mt]: https://en.wikipedia.org/wiki/Merkle_tree
