# Delta Manifests

<div class="sidebar">Do not confuse these with the core [Fossil delta
format](./delta_format.wiki). This document describes an optional
feature not enabled by default.</div>

This article describes "delta manifests," a special-case form of
checkin manifest which is intended to take up far less space than
a normal checkin manifest, in particular for repositories with
many files. We'll see, however, that the space savings, if indeed
there are any, come with some caveats.

This article assumes that the reader is at least moderately familiar
with Fossil's [artifact file format](./fileformat.wiki), in particular
the structure of checkin manifests, and it won't make much sense to
readers unfamiliar with that topic.

# Background and Motivation of Delta Manifests

A checkin manifest includes a list of every file in that checkin.  A
moderately-sized project can easily have a thousand files, and every
checkin manifest will include those thousand files. As of this writing
Fossil's own checkins contain 989 files and the manifests are 80kb
each. Thus a checkin which changes only 2 bytes of source code
ostensibly costs another 80kb of storage for the manifest for that
change.

Delta manifests were conceived as a mechanism to help combat that
storage overhead.

# Makeup of a Delta Manifest

A delta manifest is structured like a normal manifest (called a
"baseline" manifest) except that it has *two types of parents*: the
P-card which is part of (nearly) every manifest and a so-called
baseline (denoted by a B-card). The P-card tells us which artifact(s)
is/are the parents for purposes of the SCM version DAG. The B-card
tells us which manifest to use as a basis for this delta. The B-card
need not be, and often is not, the same as the P-card. Here's an
example:

```
B c04ce8aaf1170966c6f8abcce8b57e72a0fa2b81
C Minor\sdoc\supdates...
D 2021-03-11T18:56:24.686
F bindings/s2/shell_extend.c 6d8354c693120a48cfe4798812cd24499be174b2
<15 F-cards snipped for brevity>
F src/repo.c 2f224cb0e59ccd90ba89c597e40b8e8d87506638
P 61d3e64e6fb1a93d4a7b0182e4c6b94d178d66d9
R a84ec2e8e1eb37ff0d94cac262795e23
U stephan
Z 536e6d26dd8dbe2779d9e5f52a15518e
```

The B-card names another manifest, by its unique ID, the same way that
a P-card does. A manifest may have multiple P-card parents (the second
and subsequent ones denoting merge parents) but B-cards always refer
to exactly one parent.

What unambiguously distinguishes this as a delta is the existence of
the B-card. All deltas have a B-card and no other type of artifact has
one. What also, but not unambiguously, distinguishes it as a delta is
that it has only 17 F-cards, whereas a baseline manifest in that same
repository has (as of this writing) 291 F-cards. In this particular
case, the delta manifest is 1363 bytes, compared to 20627 bytes for
the next checkin - a baseline manifest. That's a significant saving in
F-cards, especially if a repository contains thousands of files. That
savings, however, comes with caveats which we'll address below.

Trivia regarding the B-card:

- The B-card always refers to a baseline manifest, not another delta.
- Deltas may not chain with another delta, but any number of deltas
  may have the same B-card. It is quite common for a series of delta
  manifest checkins, each of which derives (in the P-card sense) from
  the one before it, to have the same B-card.

A delta manifest is functionally identical to a normal manifest except
that it has a B-card and how it records F-cards. Namely, it only
records F-cards which have changed at some point between this delta
and the version represented by the delta's B-card. This recording of
F-card *differences* also means that delta manifests, unlike normal
manifests, have to explicitly record deleted F-cards. Baseline
manifests do not record deletions. Instead, they include a list of
every file which is part of that checkin. Deltas, however, record the
differences between their own version and a baseline version, and thus
have to record deletions. They do this by including F-cards which have
only a file name and no hash.

Iterating over F-cards in a manifest is something several important
internal parts of Fossil have to do. Iterating over a baseline
manifest, e.g. when performing a checkout, is straightforward: simply
walk through the list in the order the cards are listed. A delta,
however, introduces a significant wrinkle to that process. In short,
when iterating over a delta's F-cards, code has to compare the delta's
list to the baseline's list. If the delta has an entry the parent does
not have, or which is a newer entry for the same file, the delta's
entry is used. If the delta is missing an entry which the baseline
has, the baseline's entry is used. When a deletion F-card is
discovered in the delta (recall that baselines do not record
deletions), iteration over that card is skipped - the internal
algorithms which iterate over F-cards never report deletions to the
code iterating over those cards. The reason for that is consistency:
only deltas record file deletions, but the fact that it's a delta is
an internal detail, not something which higher-level code should
concern itself with. If higher-level iteration code were shown file
deletions, they would effectively be dealing with a leaky abstraction
and special-case handling which only applies to delta manifests. The
F-card iteration API hides such details from its users (other
Fossil-internal APIs).


# When does Fossil Create Deltas?

By default, Fossil never creates delta manifests. It can be told to do
so using the `--delta` flag to the [`commit`
command](/help/commit). (Before doing so in your own repositories,
please read the section below about the caveats!) When a given
repository gets a delta manifest for the first time, Fossil records
that fact in the repository's `config` table with an entry named
`seen-delta-manifest`. If, in later sessions, Fossil sees that that
setting has a true value, it will *consider* creating delta manifests
by default.

Conversely, the [`forbid-delta-manifests` repository config
setting](/help/forbid-delta-manifests) may be used to force Fossil to
*never* create deltas. That setting will propagate to other repository
clones via the sync process, to try to ensure that no clone introduces
a delta manifests. We'll cover reasons why one might want to use that
setting later on.

After creating a delta manifest during the commit process, Fossil
examines the size of the delta. If, in Fossil's opinion, the space
savings are not significant enough to warrant the delta's own
overhead, it will discard the delta and create a new baseline manifest
instead. (The heuristic it uses for that purpose is tucked away in
Fossil's checkin algorithm.)


# Caveats

Delta manifests may appear, on the surface, to be a great way to save
a few bytes of repository space. There are, however, caveats...

## Space Savings?

Though deltas were conceived as a way to save storage space, that
benefit is *not truly achieved* because...

When a manifest is created, Fossil stores its parent version as a
[fossil delta](./delta_format.wiki) (as opposed to a delta manifest)
which succinctly descibes the differences between the parent and its
new child. This form of compression is extremely space-efficient and
can reduce the real storage space requirements of a manifest from tens
or hundreds of kilobytes down to a kilobyte or less for checkins which
modify only a few files. As an example, as of this writing, Fossil's
[tip checkin baseline manifest](/artifact/decd537016bf) is 80252 bytes
(uncompressed), and the delta-compressed baseline manifest of the
[previous checkin](/artifact/2f7c93f49c0e) is stored as a mere 726
bytes of Fossil-delta'd data (not counting the z-lib compression which
gets applied on top of that). In this case, the tip version modified 7
files compared to its parent version.

Thus delta manifests do not *actually* save much storage space. They
save *some*, in particular in the tip checkin version: Fossil
delta-compresses *older* versions of checkins against the child
versions, as opposed to delta-compressing the children against the
parents. The reason is to speed up access for the most common case -
the latest version. Thus tip-version delta manifests are more
storage-space efficient than tip-version baseline manifests. Once the
next version is committed, though, and Fossil deltification is applied
to those manifests, that difference in space efficiency shrinks
tremendously, often to the point of insignificance.

We can observe the Fossil-delta compression savings using a bit of
3rd-party code which can extract Fossil-format blobs both with and
without applying their deltas:

```
$ f-acat tip > A        # tip version's manifest
$ f-acat prev --raw > B # previous manifest in its raw fossil-deltified form
$ f-acat prev > C       # previous manifest fossil-undelta'd
$ ls -la A B C
-rw-rw-r-- 1 user user 80252 Mar 12 07:09 A  # tip
-rw-rw-r-- 1 user user   726 Mar 12 07:09 B  # previous: delta'd
-rw-rw-r-- 1 user user 80256 Mar 12 07:09 C  # previous: undelta'd
```

For comparison's sake, when looking at a separate repository which
uses delta manifests, a delta-compressed delta manifest takes up
approximately the same space as a delta-compressed baseline manifest
(to within 10 bytes for the test samples).

i.e. delta manifests may not save any storage space except for the tip
version! (*Surprise!*)

In terms of RAM costs, deltas usually cost more memory than baseline
manifests. The reason is because traversing a delta requires having
not only that delta in memory, but also its baseline version. Delta
manifests are seldom used in ways which do not require also loading
their baselines. Thus Fossil internally requires two manifest objects
for most operations with a delta manifest, whereas a baseline has but
one. The difference in RAM cost is directly proportional to the size
of the delta manifest.

## Manifests as Proof of Code Integrity

Delta manifests have at least one more notable caveat, this one
arguably more significant than an apparent lack of space savings:
they're useless for purposes of publishing a manifest which downstream
clients can use to verify the integrity of their copy of the software.

Consider this use case: [the SQLite project](https://sqlite.org)
publishes source code to many thousands of downstream consumers, many
of whom would like to be able to verify that the copy they have
downloaded is actually the copy published by the project. This is
easily achieved by providing a copy of the downloaded version's
manifest, as it contains a hash of every single file the project
published and the manifest itself has a well-known hash and is
cryptographically tamper-proof. It's mathematically extremely improbable for a
malicious party to modify such a manifest and re-publish it as an
"official" one, as the various hashes (F-cards, R-card, Z-card, *and*
the hash of the manifest itself) would not line up. A collision-based
attack would have to defeat *all four of those hashes*, which is
practically impossible to do. Thus a Fossil checkin manifest can be used
to provide strong assurances that a given copy of the software has not
been tampered with since being exported by Fossil.

*However*, that use case is *only possible with baseline manifests*.
A delta manifest is *essentially useless* for that purpose. The
algorithm for traversing F-cards of a delta manifest is not trivial
for arbitrary clients to reproduce, e.g. using a shell script. While
it *could* be done in any higher-level programming language (or some
truly unsightly shell code), it would be an onerous burden on
downstream consumers and would not be without risks of having bugs
which invalidate the strong guarantees provided by the manifest.

It's worth noting that the core Fossil project repository does not use
delta manifests, at least in part for the same reason the SQLite
project does not: the ability to provide a manifest which clients can
easily use to verify the integrity of the code they've downloaded. The
[`forbid-delta-manifests` config
setting](/help/forbid-delta-manifests) is used to ensure that none are
introduced into the repository beyond the few which were introduced
solely for testing purposes.

