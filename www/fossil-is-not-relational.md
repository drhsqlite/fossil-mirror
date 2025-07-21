# Fossil is not Relational

***An Introduction to the Fossil Data Model***

Upon hearing that Fossil is based on sqlite, it's natural for people
unfamiliar with its internals to assume that Fossil stores its
SCM-relevant data in a database-friendly way and that the SCM history
can be modified via SQL. The truth, however, is *far stranger than
that.*

This document introduces, at a relatively high level:

1) The underlying enduring and immutable data format, which is
  independent of any specific storage engine.

2) The `blob` table: Fossil's single point of SCM-relevant data
  storage.

3) The transformation of (1) from its immutable raw form to a
  *transient* database-friendly form.

4) Some of the consequences of this model.


# Part 1: Artifacts

```pikchr center
AllObjects: [
A: file "Artifacts" fill lightskyblue;
down; move to A.s; move 50%;
F: file "Client" "files";
right; move 1; up; move 50%;
B: cylinder "blob table"
right;
arrow from A.e to B.w;
arrow from F.e to B.w;
arrow dashed from B.e;
C: box rad 0.1 "Crosslink" "process";
arrow
AUX: cylinder "Auxiliary" "tables"
arc -> cw dotted from AUX.s to B.s;
] # end of AllObjects
```


The centerpiece of Fossil's architecture is a data format which
describes what we call "artifacts." Each artifact represents the state
of one atomic unit of SCM-relevant data, such as a single checkin, a
single wiki page edit, a single modification to a ticket, creation or
cancellation of tags, and similar SCM constructs. In the cases of
checkins and ticket updates, an artifact may record changes to
multiple files resp. ticket fields, but the change as a whole
is atomic. Though we often refer to both fossil-specific SCM data
and client-side content as artifacts, this document uses the term
artifact solely for the former purpose.

From [the data format's main documentation][dataformat]:

> The global state of a fossil repository is kept simple so that it
> can endure in useful form for decades or centuries. A fossil
> repository is intended to be readable, searchable, and extensible by
> people not yet born.

[dataformat]: ./fileformat.wiki

This format has the following major properties:

- It is <u>**syntactically simple**</u>, easily and efficiently
  parsable in any programming language. It is also entirely
  human-readable.

- It is <u>**immutable**</u>. An artifact is identified by its unique
  hash value. Any modification to an artifact changes that hash,
  thereby changing its identity.

- It is <u>**not generic**</u>. It is custom-made for its purpose and
  makes no attempt at providing a generic format. It contains *only*
  what it *needs* to function, with zero bloat.

- It <u>**holds all SCM-relevant data except for client-level file
  content**</u>, the latter instead being referenced by their unique
  hash values. Storage of the client-side content is an implementation
  detail delegated to higher-level applications.
 
- <u>**Auditability**</u>. By following the hash references in
  artifacts it is possible to unambiguously trace the origin of any
  modification to the SCM state. Combined with higher-level tools
  (specifically, Fossil's database), this audit trail can easily be
  traced both backwards and forwards in time, using any given version
  in the SCM history as a starting point.

Notably, the artifact file format <u>does not</u>...

- Specify any specific storage mechanism for the SCM's raw bytes,
  which includes both artifacts themselves and client-side file
  content. The file format refers to all such content solely by its
  unique hash value.

- Specify any optimizations such as storing file-level changes as
  deltas between two versions of that content.

Such aspects are all considered to be implementation details of
higher-level applications (be they in the main fossil binary or a
hypothetical 3rd-party application), and have no effect on the
underlying artifact data model. That said, in Fossil:

- All raw byte content (artifacts and client files) is stored in
  the `blob` database table.

- Fossil uses delta and zlib compression to keep the storage size of
  changes from one version of a piece of content to the next to a
  minimum.


## Sidebar: SCM-relevant vs Non-SCM-relevant State

Certain data in Fossil are "SCM-relevant" and certain data are not. In
short, SCM-relevant data are managed in a way consistent with
controlled versioning of that data. Conversely, non-SCM-relevant data
are essentially any state neither specified by nor unambiguously
refererenced by the artifact file format and are therefore not
versioned.

SCM-relevant state includes:

- Any and all data stored in the bodies of artifacts. This includes,
  but is not limited to: wiki/ticket/forum content, tags, file names
  and Fossil-side permissions, the name of each user who introduces
  any given artifact into the data store, the timestamp of each such
  change, the inheritance tree of checkins, and many other pieces of
  metadata.

- Raw file content of versioned files. These data are external to
  artifacts, which refer to them by their hashes. How they are stored
  is not the concern of the data model, but (spoiler alert!) Fossil
  stores them in an SQLite database, one record per distinct hash, in
  its `blob` table (which we will cover more very soon).

Non-SCM-relevant state includes:

- Fossil's list of users and their metadata (permissions, email
  address, etc.). Artifacts themselves reference users only by their
  user names. Artifacts neither care whether, nor guarantee that, user
  "drh" in one artifact is in fact the same "drh" referenced in
  another artifact.

- All Fossil UI configuration, e.g. the site's skin, config settings,
  and project name.

- In short, any tables in a Fossil repository file except for the
  `blob` table. Most, but not all, of these tables are transient
  caches for the data specified by the artifact files (which are
  stored in the `blob` table), and can safely be destroyed and rebuilt
  from the collection of artifacts with no loss of state to the
  repository. *All* of them, except for `blob` and `delta`, can be
  destroyed with no loss of *SCM-relevant* data.

## Terminology Hair-splitting: Manifest vs. Artifact

We sometimes refer to artifacts as "manifests," which is technically a
term for artifacts which record checkins. The various other artifact
types are arguably not "manifests," but are sometimes referred to as
such because the internal APIs use that term.


## A Very Basic Example

The following artifact, truncated for brevity, represents a typical
checkin artifact (a.k.a. a manifest):

```
C Bug\sfix\sin\sthe\slocal\sdatabase\sfinder.
D 2007-07-30T13:01:08
F src/VERSION 24bbb3aad63325ff33c56d777007d7cd63dc19ea
F src/add.c 1a5dfcdbfd24c65fa04da865b2e21486d075e154
F src/blob.c 8ec1e279a6cd0cfd5f1e3f8a39f2e9a1682e0113
<SNIP>
F www/selfcheck.html 849df9860df602dc2c55163d658c6b138213122f
P 01e7596a984e2cd2bc12abc0a741415b902cbeea
R 74a0432d81b956bfc3ff5a1a2bb46eb5
U drh
Z c9dcc06ecead312b1c310711cb360bc3
```

Each line is a single data record called a "card." The first letter of
each line tells us the type of data stored on that line and the
following space-separated tokens contain the data for that
line. Tokens which themselves contain spaces (notably the checkin
comment) have those escaped as `\s`. The raw text of wiki
pages/comments, forum posts, and ticket bodies/comments is stored
directly in the corresponding artifact, but is stored in a way which
makes such escaping unnecessary.

The hashes seen above are a critical component of the architecture:

- The `F` (file) records refer to the content of those files by the
hash of that content. Where that content is stored is *not* specified
by the data model.

- The `P` (parent) line is the hash code of the parent version (itself
  an artifact).

- The `Z` line is a hash of all of the content of *this artifact*
  which precedes the `Z` line. Thus any change to the content of an
  artifact changes both the artifact's identity (its hash) and its `Z`
  value, making it impossible to inject modified artifacts into an
  existing artifact tree.

- The `R` line is yet another consistency-checking hash which we won't
  go into here except to say that it's an internal consistency
  check/line of defense against modification of file content
  referenced by the artifact.

# Part 2: The `blob` Table

```pikchr center
AllObjects: [
A: file "Artifacts";
down; move to A.s; move 50%;
F: file "Client" "files" fill lightskyblue;
right; move 1; up; move 50%;
B: cylinder "blob table" fill lightskyblue;
right;
arrow from A.e to B.w;
arrow from F.e to B.w;
arrow dashed from B.e;
C: box rad 0.1 "Crosslink" "process";
arrow
AUX: cylinder "Auxiliary" "tables"
arc -> cw dotted from AUX.s to B.s;
] # end of AllObjects
```


The `blob` table is the core-most storage of a Fossil repository
database, storing all SCM-relevant data (and *only* SCM-relevant
data). Each row of this table holds a single artifact or the content
for a single version of a single client-side file. Slightly truncated
for clarity, its schema contains the following fields:

- **`uuid`**: the hash code of the blob's contents.
- **`rid`**: a unique integer key for this record. This is how the
  blob table is mapped to other (transient) tables, but the RIDs are
  specific to one given copy of a repository and must not be used for
  cross-repository referencing. The RID is a private/internal value of
  no use to a user unless they're building SQL queries for use with
  the Fossil db schema.
- **`size`**: the size, in bytes, of the blob's contents, or -1 for
  "phantom" blobs (those which Fossil knows should exist because it's
  seen them referenced somewhere, but for which it has not been given
  any content).
- **`content`**: the blob's raw content bytes, with the caveat that
  Fossil is free to store it in an "alternate representation."
  Specifically, the `content` field often holds a zlib-compressed
  delta from a previous version of the blob's content (a separate
  entry in the `blob` table), and an auxiliary table named `delta`
  maps such blobs to their previous versions, such that Fossil can
  reconstruct the real content from them by applying the delta to its
  previous version (and such deltas may be chained). Thus extraction
  of the content from this field cannot be performed via vanilla SQL,
  and requires a Fossil-specific function which knows how to convert
  any internal representations of the content to its original form.


## Sidebar: How does `blob` Distinguish Between Artifacts and Client Content?

Notice that the `blob` table has no flag saying "this record is an
artifact" or "this record is client data." Similarly, there is no
place in the database dedicated to keeping track of which `blob`
records are artifacts and which are file content.

That said, (A) the type of a blob can be implied via certain table
relationships and (B) the `event` table (the `/timeline`'s main data
source) incidentally has a list of artifacts and their sub-types
(checkin, wiki, tag, etc.). However, given that all of those
relationships, including the timeline, are *transient*, how can Fossil
distinguish between the two types of data?

Fossil's artifact format is extremely rigid and is *strictly* enforced
internally, with zero room provided for leniency. Every artifact which
is internally created is re-parsed for validity before it is committed
to the database, making it impossible that Fossil can inject an
invalid artifact into the repository. Because of the strictness of the
artifact parser, the chances that any given piece of arbitrary client
data could be successfully parsed as an artifact, even if it is
syntactically 99% similar to an artifact, are *effectively zero*.

Thus Fossil's rule of interpreting the contents of the blob table is:
if it can be parsed as an artifact, it *is* an artifact, else it is
opaque client-side data.

That rule is most often relevant in operations like `rebuild` and
`reconstruct`, both of which necessarily have to sort out artifacts
and non-artifact blobs from arbitrary collections of blobs.

It is, in fact, possible to store an artifact unrelated to the current
repository in that repository, and it *will be parsed and processed as
an artifact* (see below), but it likely refers to other artifacts or
blobs which are not part of the current repository, thereby possibly
introducing "strange" data into the UI. If this happens, it's
potentially slightly confusing but is functionally harmless.


# Part 3: Crosslinking

```pikchr center
AllObjects: [
A: file "Artifacts";
down; move to A.s; move 50%;
F: file "Client" "files";
right; move 1; up; move 50%;
B: cylinder "blob table"
right;
arrow from A.e to B.w;
arrow from F.e to B.w;
arrow dashed from B.e;
C: box rad 0.1 "Crosslink" "process" fill lightskyblue;
arrow
AUX: cylinder "Auxiliary" "tables" fill lightskyblue;
arc -> cw dotted from AUX.s to B.s;
] # end of AllObjects
```

Once an artifact is stored in the `blob` table, how does one perform
SQL queries against its plain-text format? In short: *One Does Not
Simply Query the Artifacts*.

Crosslinking, as its colloquially known, is a one-way processing step
which transforms an immutable artifact's state into something
database-friendly. Crosslinking happens automatically every time
Fossil generates, or is given, a new artifact. Crosslinking of any
given artifact may update many different auxiliary tables, *all* of
which are transient in the sense that they may be destroyed and then
recreated by crosslinking all artifacts from the `blob` table (which
is exactly what the `rebuild` command does). The overwhelming majority
of individual database records in any Fossil repository are found in
these transient auxiliary tables, though the `blob` table tends to
account for the overwhelming majority of a repository's disk space.

This approach to mapping data from artifacts to the db gives Fossil
the freedom to change its database model, effectively at will, with
minimal client-side disruption (at most, a call to `rebuild`). This
allows, for example, Fossil to take advantage of new improvements in
sqlite without affecting compatibility with older repositories.

Auxiliary tables hold data mappings such as:

- Child/parent relationships of checkins. (The `plink` table.)
- Records of file names and changes to files. (The `mlink` and `filename` tables.)
- Timeline entries. (The `event` table.)

And numerous other bits and pieces.

The many auxiliary tables maintained by the app-level code reference
the `blob` table via its RID field, as that's far more efficient than
using hashes (`blob.uuid`) as foreign keys. The contexts of those
auxiliary data unambiguously tell us whether the referenced blobs are
artifacts or file content, so there is no efficiency penalty there for
hosting both opaque blobs and artifacts in the `blob` table.

The complete SQL schemas for the core-most auxiliary tables can be found
at:

[](/finfo/src/schema.c?ci=trunk)

Noting, however, that all database tables are effectively internal
APIs, with no API stability guarantees and subject to change at any
time. Thus their structures generally should not be relied upon in
client-side scripts.


# Part 4: Implications and Consequences of the Model

*Some* of the implications and consequences of Fossil's data model
combined with the higher-level access via SQL include:

- **Provable immutability of history.** Fossil offers only one option
  for modifying history: "shunning" is the forceful removal of an
  artifact from the `blob` table and the creation of a db record
  stating that the shunned hash may no longer be synced into this
  repository. Shunning effectively leaves a hole in the SCM history,
  and is only intended to be used for removal of illegal, dangerous,
  or private information which should never have been added to the
  repository.

- **Complete separation of SCM-relevant data and app-level data
  structures**. This allows the application to update its structures
  at will without significant backwards-compatibility concerns. In
  Fossil's case, "data structures" primarily refers to the SQL
  schema. Bringing a given repository schema up to date vis a vis a
  given fossil binary version simply means rebuilding the repository
  with that fossil binary. There are exceptionally rare cases, namely
  the switch from SHA1 to SHA3-256 ushered in with Fossil 2.0, which
  can lead to true incompatibility. e.g. a Fossil 1.x client cannot
  use a repository database which contains SHA3 hashes, regardless of
  a rebuild.

- **Two-way compatibility with other hypothetical clients** which also
  implement the same underlying data model. So far there are none, but
  it's conceivably possible.

- **Provides a solid basis for reporting.** Fossil's real-time metrics
  and reporting options are arguably the most powerful and flexible
  yet seen in an SCM.

- Very probably several more things.
