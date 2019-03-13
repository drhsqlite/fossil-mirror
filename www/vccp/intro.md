Version Control Collaboration Protocol
======================================

<blockquote><center style='background: yellow; border: 1px solid black;'>
This document is a work in progress.<br>
The last update was on 2019-03-13.<br>
Check back later for updates.
</center></blockquote>

1.0 Introduction
----------------

The Version Control Collaboration Protocol or VCCP is an attempt to make
it easier for developers to collaborate even when they are using different
version control systems.

For example, suppose Alice, the founder and principal maintainer
for the fictional "BambooCoffee" project, prefers using the
[Mercurial](https://www.mercurial-scm.org/) version control system,
but two of her clients, Bob and Cindy, know nothing but
[Git](https://www.git-scm.org/) and steadfastly refuse to
type any command that begins with "hg".
Further suppose that an important
collaborator, Dave, really prefers [Bazaar](bazaar.canonical.com/).
The VCCP is designed to make it relatively easy and painless
for Alice to set up Git and Bazaar mirrors of her Mercurial
repository so that Bob, Cindy, and Dave can all use the tools
they are most familiar with.

<center>![](diagram-1.jpg)</center>

Assuming all the servers speak VCCP (which is not the case at the
time of this writing, but we hope to encourage that for the future)
then whenever Alice checks in a new change to her primary repository
(here labeled "Truth") that repository sends a VCCP message to the
two mirrors which causes them to pick up the changes as well.

### 1.1 Bidirectional Collaboration

The diagram above shows that all changes originate from Alice and
that Bob, Cindy, and David are only consumers. If Cindy wanted to
make a change to BambooCoffee, she would have to do that with a backchannel,
such as sending a patch via email to Alice and asking Alice to check
in the change.

But VCCP also support bidirectional collaboration.

<center>![](diagram-2.jpg)</center>

If Cindy is a frequent contributor, and assuming that Git and Mercurial
are compatible version control systems (which I believe they are) then
VCCP can be used to move information from Truth to Mirror-1 and from Mirror-1
back to Truth.  In that configuration, Cindy would be able to check in her
changes using the "git" command.  The Mirror-1 server would then send a
VCCP message back to Truth containing Cindy's changes.  Truth would then
relay those changes over to Mirror-2 where Dave could see them as well.

### 1.2 Client-Mirror versus Server-Mirror

VCCP allows the mirrors to be set up as either clients or servers.

In the client-mirror approach, the mirrors periodically poll Truth asking
for changes.  In the server-mirror approach, Truth sends changes to the
mirrors as they occur.

In the first example above, the implication was that the server-mirror
approach was being used.  The Truth repository would take the initiative
to send changes to the mirrors. But it does not have to be that way.
Suppose Dave is unknown to Alice.  Suppose he just likes Alice's work and
wants to keep his own mirror of her work for his own convenience.
Dave could set up
Mirror-2 as a client-mirror that periodically polls Truth for changes.

In the second example above, Truth and Mirror-1 could be configured to
have a Peer-to-Peer relationship rather than a Truth-to-Mirror relationship.
When new content arrives at Truth (because Alice did an "hg commit"), 
Truth acts as a client to initiate a transfer of that new information 
over to Mirror-1.  When new content originates at Mirror-1 (because
Cindy did "git commit") then Mirror-1 acts as a client to send a the new
content over the Truth.  Or, they could set it up so that Truth is always
the client and it periodically polls Mirror-1 looking for new content
coming from Cindy.  Or, they could set it up so that Mirror-1 is always
the client and it periodically polls Truth looking for changes from Alice.

The point is that VCCP works in all of these scenarios.

### 1.3 Name Mapping

Different version control systems use different names to refer to the same
object.  For example, Fossil names files using a SHA3-256 hash of the
unmodified file content, whereas Git uses a hardened-SHA1 hash of the file
content with an added prefix. Mercurial, Monotone, Bazaar, and others all
uses different naming schemes, so that the same check-in in any particular
version control system will have a different name in all other version
control systems.

When mirroring a project between two version control systems, somebody
needs to keep track of the mapping between names.

For example, in the second diagram above, if Mirror-1 wants to tell Truth
that it has a new check-in "Q" that is a child of "P", then it has to send
the name of check-in "P".  Does it send the Git-name of "P" or the
Mercurial-name of "P"?  If Mirror-1 sends Truth the Git-name of "P" then
Truth must be the system that does the name mapping.  If Mirror-1 sends
Truth the Mercurial-name of "P", then Mirror-1 is the system that maintains
the mapping.

The VCCP is designed such that both names for a
particular check-in or file can be sent.  One of the collaborating systems
must still take responsibility for translating the names, but it does not
matter which system.  As long as one or the other of the two systems
maintains a name mapping, the collaboration will work.  Of course, it
also works for both systems to maintain the name map, and for maximum
flexibility, perhaps that should be the preferred approach.

2.0 Minimum Requirements
------------------------

The VCCP is modeled after the Git fast-export and fast-import protocol.
That is to say, VCCP thinks in terms of "check-ins" with each check-in
consisting of a number of files (or "Blobs" in git-speak).  Any version
control system that wants to use VCCP needs to also be able to think
in those terms.

Since VCCP is modeled after fast-import, it has the concept of a tag.  
But the use of tags is optional and
VCCP will work with systems that do not support tags.

VCCP assumes that most check-ins have a parent check-in from which it
was derived. Obviously, the first check-in for a project does not have
a parent, but all the others should.  Check-ins may also identify
zero or more "merge" parents, and zero or more "cherrypick" ancestors.
But the merges and cherrypicks can be ignored on systems that do not
support those concepts.

VCCP assumes that every distinct version of a file and every check-in has
a unique name.  In Git and Mercurial, those names are SHA1 hashes
(computed in different ways).  Fossil uses SHA3-256 hashes.  I'm not sure
what Bazaar uses.  VCCP does not care how the names are derived, as long
as they always uniquely identify the file or check-in.

VCCP assumes that each check-in has a commit comment and a "committer"
and a timestamp for when the commit occurred.  We hope that the timestamps
are well-ordered in the sense that each check-in comes after its predecessor,
though this is not a requirement.  VCCP will continue to work even if
the timestamps are out of order, perhaps due to a misconfigured system clock
on the workstation of one of the collaborators.

3.0 Protocol Overview
---------------------

The VCCP is a client-server protocol.
A client formats a VCCP message and sends it to the server.
The server acts upon that message, formulates a reply, and sends
the reply back to the client.

It does not matter what transport mechanism is used to send the VCCP
messages from client to server and back again.
But for maximum flexibility, it is suggested that HTTP (or HTTPS) be
used.  The client sends an HTTP request to the server with the
VCCP message as the request content and a MIME-type of "application/x-vccp".
The HTTP response is another VCCP message with the same MIME-type.
The use of HTTP means that firewalls and proxies are not an
impediment to collaboration and that collaboration connection information
can be described by a simple URL.

There are provisions in the VCCP design to allow authentication
in the body of the VCCP message itself.  Or, two systems can, by
mutual agreement, authenticate by some external mechanism.

### 3.1 Message Content

A single VCCP message round-trip can be a "push" if the client is sending
new check-in information to the server, or it can be a "pull" if the
client is polling the server to see if new check-in information is available
for download, or it can be both at once.

The basic design of a VCCP message is inspired by the Git fast-export
protocol, but with enhancements to support incremental updates and
bidirectional updates and to make the message format more robust and
portable and simpler to generate and parse.  A single message may contain
multiple "files", check-in descriptions that reference those files, and "tag"
descriptions.  A "message description" section contains authentication
data, error codes, and other meta-data.  Every request and every
reply contains, at a minimum, a message description.

For a push, the request contains a message description with
authentication information, and the new files, check-ins, and tags
that are being pushed to the server.  The reply to a push contains
success codes, and the names that the server assigned to the new objects,
so that the client can maintain a name map.

For pull, the request contains only a message description with 
authentication information and a description of what content the
client desires to pull.
The reply to a pull contains the files, check-ins, and tags requested.

For a pull request, there is no mechanism (currently defined) for the
server to learn the client-side names for files and check-ins.  Hence,
for a collaboration arrangement where the client polls the server for
updates, the client must maintain the name map.

### 3.2 Message Format Overview

The format of a VCCP message is an ordinary SQLite database file with
a two-table schema.
The DATA table contains file, check-in, and tag content and the
message description.  The DATA.CONTENT column contains either raw
file content or check-ins and tags descriptions formatted as JSON.
The message description is also JSON contained in a specially
designated row of the DATA table.  The NAME table of the schema
is used to transmit name mappings.  The NAME table serves the same
role as the "marks" file of git-fast-export.

### 3.3 Why Use A Database As The Message Format?

Why does a VCCP message consist of an SQLite database instead of a 
bespoke format like git-fast-export?

  1.  Some of the content to be transferred will typically be binary.
      Most projects have at least a few images or other binary files
      in their tree somewhere.  Other files will be pure text.  Check-in
      and tag descriptions will also be pure text (JSON).  That means
      that the VCCP message will be a mix of text and binary content.
      An SQLite database file is a convenient and efficient way 
      to encapsulate both binary and text content into a single container 
      which is easily created and accessed.

  2.  Robust, cross-platform libraries for reading and writing SQLite database
      files already exist on every computer.  No custom parser or generator
      code needs to be written, debugged, managed, or maintained.

  3.  The SQLite database file format is well defined, cross-platform
      (32-bit, 64-bit, bit-endian, and little-endian) and is endorsed
      by the US Library of Congress as a recommended file format for
      archival data storage.

  4.  Unlike a serial format (such as git-fast-export) which must
      normally be written and read sequentially from beginning to end,
      elements of an SQLite database can be constructed and read in any
      order. This gives extra implementation flexibility to both readers
      and writers.

### 3.4 Database Schema

The database schema for a VCCP message is as follows:

>
    CREATE TABLE data(
      id INTEGER PRIMARY KEY,
      dclass INT,
      sz INT,
      calg INT,
      cref INT,
      content ANY
    );
    CREATE TABLE name(
      nameid INT,
      nametype INT,
      name TEXT,
      PRIMARY KEY(nameid,nametype)
    ) WITHOUT ROWID;
        
The DATA table holds the message description, the content of files, and JSON
descriptions of check-ins and tags.  The NAME table is used to transmit
names.  The DATA table corresponds to the body of a git-fast-export stream
and the NAME table corresponds to the "marks" file that is read and
written by the "--import-marks" and "--export-marks" options of the
"git fast-export" command.

Each file, check-in, and tag is normally a single distinct entry in
the DATA table.  (Exception: very large files, greater than 1GB in size,
can be split across multiple DATA table rows - see below.) Entries in
the DATA tale can occur in any order.  It is not required that files
referenced by check-ins have a smaller DATA.ID value, for example.
Free ordering does not impede data extraction (see the algorithm descriptions
below) but it does give considerable freedom to the message generator
logic.

Each DATA row has a class identified by a small integer in the DATA.DCLASS
column.

>
| 0: | A check-in |
| 1: | A file |
| 2: | A tag |
| 3: | The VCCP message description |
| 4: | Application-defined-1 |
| 5: | Application-defined-2 |

Every well-formed VCCP message has exactly one message description entry 
with DATA.ID=0 and DATA.DCLASS=3. No other DATA table entries should have
DATA.DCLASS=3.

The application-defined values are reserved for extended uses of the
VCCP message format.  In particular, there are plans to enhance
Fossil so that it uses VCCP as its sync protocol, replacing its
current bespoke protocol.  But Fossil needs to send information other
kinds of objects, such as wiki pages and tickets, that are not known
to Git and most other version control systems.  A few
"application defined" values are available at strategic points in
the message format description to accommodate these extended use cases.
New application-defined values may be defined in the future.
Portable VCCP messages between different version control systems 
should never use the application-defined values.

The DATA.CONTENT field can be either text or binary, as appropriate.
For files, the DATA.CONTENT is binary.  For check-ins and tags and for
the message description, the DATA.CONTENT is a text JSON object.

The DATA.CONTENT field can optionally be compressed.  The DATA.SZ field
is the uncompressed size of the content in bytes.  The compression method
is determined by the DATA.CALG field:

>  
|  0: |  No compression        |
|  1: |  ZLib compression      |
|  2: |  Multi-blob            |
|  3: |  Application-defined-1 |
|  4: |  Application-defined-2 |

The "multi-blob" compression method means that the content is the
concatenation of the content in other DATA table rows.  This
allows for content that exceeds the 1GB size limit for an SQLite
BLOB column.  If the DATA.CALG field is 2, then DATA.CONTENT will
be a JSON array of integer values, where each integer is the DATA.ID
of another DATA table entry that contains part of the content.
The actual data content is the concatenation of the other DATA table
entries.  The secondary DATA table entries can also be compressed,
though not with multi-blob.  In other words, the multi-blob
compression method may not be nested.  This effectively limits the 
maximum size of a file in the VCCP to maximum size of an SQLite
database, which is 140 terabytes.

Portable VCCP files should only use compression methods 0, 1, and 2,
and preferrably only method 0 (no compression).  But application-defined
compression methods are available for proprietary uses of the
VCCP message format.  The DATA.CREF field is auxiliary data intended
for use with these application-defined compression methods.  In
particular, DATA.CREF is intended to be the DATA.ID of a "base"
entry for delta-compression methods.  For a portable VCCP file,
the DATA.CREF field should always be NULL.

The DATA.ID field provides an integer identifier for files and
check-ins.  The scope of that name is the single VCCP message
in which the DATA table entry appears, however.  The NAME table
is used to provide a mapping from these internal integer names
to the persistent global hash names of the various version
control systems.

A single object can have different names, depending on which
version control system stores it.  For this reason, the NAME
table is designed to allow storage of multiple names for the
same object.  If NAME.NAMETYPE is 0, that means that the name
is appropriate for use on the client.  If NAME.NAMETYPE is 1,
that means the name is appropriate for use on the server.

To simplify the implementation of VCCP on diverse systems,
names should be sent as text.  If the names for a particular system
are binary hashes, then the NAME table should store them as
the hexadecimal representation.

#### 3.4.1 NAME Table Example 1

Suppose a client is pushing a new check-in to the server and the
check-in text is stored in the DATA.ID=1 row.  Then the request
should contain a NAME table row with NAME.NAMEID=1 (to match the
DATA table ID value) and NAME.NAMETYPE=0 (because client names
have NAMETYPE 0) and with the name of that check-in according to
the client stored in NAME.NAME.  The server will recode the
check-in according its its own format, and store the server-side
name in a new NAME table row with NAME.NAMEID=1 and NAME.NAMETYPE=1.
The server then includes the complete NAME table in its reply
back to the client.  In this way, the client is able to discover
the name of the check-in on the server.  The serve can also
remember the client check-in name, if desired.

### 3.5 Check-in JSON Format

Check-ins are described by DATA table rows where the content is a
single JSON object, as follows:

>
    {
      "time": DATETIME,       -- Date and time of the check-in
      "comment": TEXT,        -- The original check-in comment 
      "mimetype": TEXT,       -- The mimetype of the comment text
      "branch": TEXT,         -- Branch this check-in belongs to
      "from": INT,            -- NAME.NAMEID for the primary parent
      "merge": [INT],         -- Merge parents
      "cherrypick": [INT]     -- Cherrypick merges
      "author": {             -- Author of the change
         "name": TEXT,          -- Name or handle
         "email": TEXT,         -- Email address
         "time": DATETIME       -- Override for $.time
      },
      "committer": {          -- Committer of the change
        "name": TEXT,           -- Name or handle
        "email": TEXT,          -- Email address
        "time": DATETIME        -- Override for $.time
      },
      "tag": [{               -- Tags and properties for this check-in
         "name": TEXT,          -- tag name
         "value": TEXT,         -- value (if it is a property)
         "delete": 1,           -- If present, delete this tag
         "propagate": 1         -- Means propagate to descendants
      }],
      "reset": 1,             -- All files included, not just changes
      "file": [{              -- File in this check-in
        "fname": TEXT,          -- filename
        "id": INT,              -- DATA.ID or NAME_NAMEID. Omitted to delete
        "mode": TEXT,           -- "x" for executable.  "l" for symlink
        "oldname": TEXT         -- Prior name if the file is renamed
      }]
    }

The $.time element is defines the moment in time when the check-in
occurred.  The $.time field is required.  Times are always Coordinated
Universal Time (UTC).  DATETIME can be represented in multiple ways:

  1.  If the DATETIME is an integer, then it is the number of seconds
      since 1970 (also known as "unix time").

  2.  If the DATETIME is text, then it is ISO8601 as follows:
      "YYYY-MM-DD HH:MM:SS.SSS".  The fractional seconds may be
      omitted.

  3.  If the DATETIME is a real number, then it is the fractional
      julian day number.

The $.comment element is the check-in comment.  The $.comment field is
required.  The mimetype for $.commit defaults to "text/plain" but can 
be some other MIME-type if the $.mimetype field is present.

The $.branch element defines the name of the branch that this check-in
belongs to.  If omitted, the branch of the check-in is the same as
the branch of its primary parent check-in.

The $.from element is defines the primary parent check-in.  Every
check-in other than the first check-in of the project has a primary
parent.  The integer value of the $.from element is either the
DATA.ID value for another check-in in the same VCCP message or is
the NAME.NAMEID value for a NAME table entry that identifies the
parent check-in, or both.  If the information sender is relying on the
other side to do name mapping, then only the local name will be provided.
But if the information sender has a name map, it should provide both
its local name and the remote name for the check-in, so that the receiver
can update its name map.

The $.merge element is an array of integers for additional check-ins
that are merged into the current check-in.  The $.cherrypick element
is an array of integer values that are check-ins that are cherrypick-merged
into the current check-in.  Systems that do not record cherrypick merges
can ignore the $.cherrypick value.

The $.author and $.committer elements define who created the check-in.
The $.committer element is required.  The $.author element may be omitted
in the common case where the author and committer are the same.  The
$.committer.time and $.author.time subelements should only be included
if they are different from $.time.

The $.reset element, if present, should have an integer value of "1".
The presence of the $.reset element is a flag that affects the meaning
of the $.file element.

The $.file element is an array of JSON objects that define the files
associated with the check-in.  If the $.reset flag is present, then there
must be one entry in $.file for every file in the check-in.  If the
$.reset flag is omitted (the common case) then there is one entry
in $.file for every file that changes relative to the primary parent
in $.from.  If There is no primary parent, then the presence of the
$.reset flag is assumed even if it is omitted.

The $.file[].fname element is the name of the file.
The $.file[].id element corresponds to a DATA.ID or NAME.NAMEID
that is the content of the file.  If the file is being removed
by this check-in, then the $.file[].id element is omitted.
The $.file[].mode element is text containing one or more ASCII
characters.  If the "x" character is included in $.file[].mode
then the file is executable.  If the "l" character is included
in $.file[].mode then the file is a symbolic link (and the content
of the file is the target of the link).  The $.file[].mode may
be blank or omitted for a normal read/write file.  If a file
is being renamed, the $.file[].oldname field may be included
to show the previous name of the file, if that information is
available.

Some version control systems allow tags and properties to be
associated with a check-in.  The $.tag element supports this
feature.  Each element of the $.tag array is a separate tag
or property.  If the $.tag[].propagate field exists and has
a value of "1", then the tag/property propagates to all
non-merge children.  If the $.tag[].delete field exists and
has a value of "1", then a propagating tag or property with 
the given name that was set by some ancestor check-in is
stopped and omitted from this check-in.  Version control
systems that do not support tags and/or properties on check-ins
or that do not support tag propagation can ignore all of these
attributes.
