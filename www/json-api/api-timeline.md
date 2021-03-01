# JSON API: /timeline
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Introduction](#intro)
* [Branch Timeline](#branch)
* [Technote (formerly Event) Timeline](#technote)
* [Ticket Timeline](#ticket)
* [Wiki Timeline](#wiki)

---

<a id="intro"></a>
# Introduction

These requests return overview-level information about various types of
changes. The response payload differs for each artifact type, and the
current structures are almost certainly not "final" (e.g. we are still
undecided on how/whether to handle artifact links within commit messages
and whatnot).

By default the entries are returned in chronological order from newest
to oldest, but some options might change that.

FIXME (20120623): we have some inconsistent `type` vs. `eventType` in
the result sets. `type` is the current preferred choice (and it seems
unlikely that `eventType` is actually used in any client code). We
don't actually need either one (but a use for `type` is easily
envisioned), and we may get rid of both.

**Common request options (via CLI, GET or POST.payload):**

-   `limit=integer` limits the number of entries in the response. Default
    is unspecified (but is "quite possibly 20 or so"). A limit value of
    0 disables any limit, fetching all entries (which can take a really
    long time and might overload clients which have very limited
    memory).\  
    CLI mode: `--limit|-n #`
-   `after="YYYY-MM-DD[ HH:mm:ss]"` limits the search to items on or
    after the given date string. Reverses the normal timeline sort
    order. Alias: "a". Only one of "after" or "before" can be used, and
    if both are specified then which one takes precedence is
    unspecified.\  
    CLI mode: `--after|-a "DATE[ TIME]"`
-   `before="YYYY-MM-DD[ HH:mm:ss]"` limits the search to items on or
    before the given date string.\  
    CLI mode: `--before|-b "DATE[ TIME]"
-   TODOs, still to be ported from the HTML-mode timeline:
    -   circa=DATETIME
    -   tag=string
    -   related=tag name
    -   string=search string

<a id="branch"></a>
# Branch Timeline

**Status:** partially implemented but undocumented because the utility
of the current impl is under question. It also doesn't understand most
of the common timeline options.

<a id="checkin"></a>
# Checkin Timeline

**Status:** implemented 201109xx

**Required privileges:** "o"

**Request:** `/json/timeline/checkin`

**Response payload example:**

```json
{
"limit": number, /* if not set, all records are returned */
"timeline":[{
  "uuid":"be700e84336941ef1bcd08d676310b75b9070f43",
  "timestamp":1317094090,
  "comment":"Added /json/timeline/ci showFiles to ajax test page.",
  "user":"stephan",
  "isLeaf":true,
  "bgColor":null, /* not quite sure why this is null? */
  "type":"ci",
  "parents": ["primary parent hash", "...other parent hashes"],
  "tags":["json"],
  "files":[{
    "name":"ajax/index.html",
    "uuid":"9f00773a94cea6191dc3289aa24c0811b6d0d8fe",
    "parent":"50e337c33c27529e08a7037a8679fb84b976ad0b",
    "state":"modified"
   }]
 },...]
}
```

(Achtung: the `parents` property was called `prevUuid` prior to 20120316.)

The `parents` property lists the checkins which were parents of this
commit. The first entry in the array is the "primary parent" - the one
which was not involved in a merge with the child.

**Request options:**

-   `files=bool` toggles the addition of a "files" array property which
    contains objects describing the files changed by the commit,
    including their hash, previous hash, and state change type
    (modified, added, or removed). ([“uuid” here means hash][uvh])\  
    CLI mode: `--show-files|-f`
-   `tag|branch=string` selects only entries with the given tag or "close
    to" the given branch. Only one of these may be specified and if both
    are specified, which one takes precedence is unspecified. If the
    given tag/branch does not exist, an error response is generated. The
    difference between the two is subtle - tag filters only on the given
    tag (analog to the HTML interface's "r" option) whereas branch can
    also return entries from other branches which were merged into the
    requested branch (analog to the HTML interface's "b" option). If one
    of these is specified, the response payload will contain a "tag"
    *or* "branch" property with the tag/branch name given by the client.

<a id="technote"></a>
# Technote (formerly Event) Timeline

**Status:** implemented 20180803

**Required privileges:** "j"

**Request:**

- `/json/timeline/technote`
- DEPRECATED: `/json/timeline/event` (technotes were formerly called `events`)

**Response payload example:**

```json
{
"limit": number, /* if not set, all records are returned */
"timeline":[{
  "name":"8d18bf27e9f9ff8b9017edd55afc35701407d418",
  "uuid":"b23962c88c123924a77fd663e91af094780d920a",
  "timestamp":1478376113,
  "comment":"Style update due to [8d880f0bb4]",
  "user":"andygoth",
  "eventType":"e"
 },...]
}
```

The `uuid` of each entry can be passed to `/json/artifact` to fetch the raw
event content.

<a id="ticket"></a>
# Ticket Timeline

**Status:** implemented 201109xx

**Required privileges:** "r" or "o"

**Request:** `/json/timeline/ticket`

**Response payload example:**

```json
{
  "limit": number, /* if not set, all records are returned */
  "timeline":[{
    "uuid":"5065a5da060e181da49a618f8ae5dc245215e95b",
    "timestamp":1316511322,
    "user":"stephan",
    "eventType":"t",
    "comment":"Ticket [b64435dba9] &lt;i&gt;How to...&lt;/i&gt;",
    "briefComment":"Ticket [b64435dba9]: 2 changes",
    "ticketUuid":"b64435dba9cceb709bd54fbc5883884d73f93491"
  },...]
}
```

**Notice that there are two [hashes][uvh] for tickets** - `uuid` is the change
hash and `ticketUuid` is the actual ticket’s hash. This is an unfortunate
discrepancy vis-a-vis the other timeline entries, which only have one
hash. We may want to swap `uuid` to mean the ticket hash and change `uuid`
to `commitHash`.

<a id="wiki"></a>
# Wiki Timeline

**Status:** implemented 201109xx

**Required privileges:** "j" or "o"

**Requests:**

-   `/json/timeline/wiki`
-   `/json/wiki/timeline` (alias)

**Response payload example:**

```json
{
"limit": number, /* if not set, all records are returned */
"timeline":[{
  "uuid":"4b2026f06eb48eaf187209fcb05ba5438c3b0ef0",
  "timestamp":1331351121,
  "comment":"Changes to wiki page [Page3]",
  "user":"stephan",
  "eventType":"w"
 },...]
}
```

The `uuid` of each entry can be passed to `/json/artifact` or
`/json/wiki/get?uuid=...` to fetch the raw page and the hash of the
parent version.

[uvh]: ../hashes.md#uvh
