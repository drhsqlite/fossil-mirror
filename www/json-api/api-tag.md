# JSON API: /tag
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Add](#add)
* [Cancel](#cancel)
* [Find](#find)
* [List](#list)

---

<a id="add"></a>
# Add Tag

**Status:** implemented 20111006

**Required permissions:** "i"

**Request:** `/json/tag/add[/name[/checkin[/value]]]`

**Request options:**

-   `name=string` The tag name.
-   `checkin=string` The checkin to tag. May be a symbolic branch name.
-   raw=bool (=false) If true, then the name is set as it is provided by
    the client, else it gets "sym-" prefixed to it. Do not use this
    unless you really know what you're doing.
-   `value=string` (=null) An optional value. While tags *may* have values
    in fossil, it is unusual for them to have a value. (This probably
    has some interesting uses in custom UIs.)
-   `propagate=bool` (=false) Sets the tag to propagate to all descendants
    of the given checkin.

In CLI modes, the name, checkin, and value parameters may optionally be
supplied as positional parameters (in that order, after the command
name). In HTTP mode they may optionally be the 4th-6th path elements or
specified via GET/`POST.envelope` parameters.

**Response payload example:**

```json
{
"name":"my-tag",
"value":"abc",
"propagate":true,
"raw":false,
"appliedTo":"626ab2f3743543122cc11bc082a0603d2b5b2b1b"
}
```

The `appliedTo` property is the hash of the check-in to which the tag was
applied. This is the "resolved" version of the check-in name provided by
the client.

<a id="cancel"></a>
# Cancel Tag

**Status:** implemented 20111006

**Required permissions:** "i"

**Request:** `/json/tag/cancel[/name[/checkin]]`

**Request options:**

-   `name=string` The tag name. May optionally be provided as the 4th path
    element.
-   `checkin=string` The checkin to untag. May be a symbolic branch name.
    May optionally be provided as the 5th path element.

In CLI modes, the name and checkin parameters may optionally be supplied
as positional parameters (in that order, after the command name) or
using the `-name NAME` and `-checkin NAME` options. In HTTP mode they may
optionally be the 4th and 5th path elements.

**Response payload:** none (resultCode indicates failure)


<a id="find"></a>
# Find Tag

Fetches information about artifacts having a particular tag.

Achtung: the output of this response is based on the HTML-mode
implementation, but it's not yet certain that it's exactly what we want
for JSON mode. The request options and response format may change.

**Status:** implemented 20111006

**Required permissions:** "o"

**Request:** `/json/tag/find[/tagName]`

The response format differs somewhat depending on the options:

-   `name=string` The tag name to search for. Can optionally be the 3rd
    path element.
-   `limit=int` (defalt=0) Limits the number of results (0=no limit).
    Since they are ordered from oldest to newest, the newest N results
    will be returned.
-   `type=string` (default=`*`) Searches only for the given type of
    artifact (using fossil's conventional type naming: ci, e, t, w.)
-   `raw=bool` (=false) If enabled, the response is an array of hashes
    of the requested artifact type; otherwise,
    it is an array of higher-level objects. If this is
    true, the "name" property is interpreted as-is. If it is false, the
    name is automatically prepended with "sym-" (meaning a branch).
    (FIXME: the current semantics are confusing and hard to remember.
    Re-do them.)

**Response payload example, in RAW mode: (expect this format to change
at some point!)**

```json
{
"name":"sym-trunk"
"raw":true,
"type":"*",
"limit":2,
"artifacts":[
  "a28c83647dfa805f05f3204a7e146eb1f0d90505",
  "dbda8d6ce9ddf01a16f6c81db2883f546298efb7"
 ]
}
```

Very likely todo: return more information with that (at least the
artifact type and timestamp). Once the `/json/artifact` family of bits is
finished we could use that to return artifact-type-dependent values
here.

**Response payload example, in non-raw mode:**

```
{
"name":"trunk",
"raw":false,
"type":"*",
"limit":1,
"artifacts":[{
  "uuid":"4b0f813b8c59ac8f7fbbe33c0a369acc65407841",
  "timestamp":1317833899,
  "comment":"fixed [fc825dcf52]",
  "user":"ron",
  "eventType":"checkin"
 }]
}
```

<a id="list"></a>
# List Tags

**Status:** implemented 20111006

**Required permissions:** "o"

**Request:** `/json/tag/list[/checkinName]`

Potential fixme: we probably have too many different response formats
here. We should probably break this into multiple subcommands.

The response format differs somewhat depending on the options:

-   `checkin=string` Lists the tags only for the given CHECKIN (can be a
    branch name). If set, includeTickets is ignored (meaningless in this
    combination). This option can be set as either a GET/`POST.payload`
    option, as the last element of the request path, e.g.
    `/json/tag/list/MYBRANCH` *or* with `POST.payload` set to a string
    value.
-   `raw=bool` (default=false) uses "raw" tag names
-   `includeTickets=bool` (default=false) Determines whether `tkt-` tags
    are included. There is one of these for each ticket, so there can be
    many of them (over 900 in the main fossil repo as of this writing).

**Response format when raw=false and no checkin is specified:**

```json
{
"raw":false,
"includeTickets":false,
"tags":[
  "bgcolor",
  "branch",
  "closed",
  …all tag names...
 ]
}
```

Enabling the `raw` option will leave the internal `sym-` prefix on tags
which have them but will not change the structure. If `includeTickets` is
true then `tkt-` entries (possibly very many!) will be included in the
output, else they are elided.

**General notes:**

The `tags` property will be null if there are no tags, every non-empty
repo has at least one tag (for the trunk branch).

**Response format when raw=false and checkin is specified:**

```json
{
"raw":false,
"tags":{
  "json":null,
  "json-multitag-test":null
 }
}
```

The `null`s there are the tag values (most tags don't have values).

If `raw=true` then the tags property changes slightly:

```json
{
"raw":true,
"tags":{
  "branch":"json",
  "sym-json":null,
  "sym-json-multitag-test":null
 }
}
```

TODO?: change the tag values to objects in the form
`{value:..., tipUuid:string, propagating:bool}`.
