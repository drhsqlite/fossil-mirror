# JSON API: /artifact
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Checkin Artifacts (Commits)](#checkin)
* [File Artifacts](#file)
* [Wiki Artifacts](#wiki)

---

<a id="checkin"></a>
# Checkin Artifacts (Commits)

Returns information about checkin artifacts (commits).

**Status:** implemented 201110xx

**Request:** `/json/artifact/COMMIT_HASH`

**Required permissions:** "o" (was "h" prior to 20120408)

**Response payload example: (CHANGED SIGNIFICANTLY ON 20120713)**

```json
{
"type":"checkin",
"name":"18dd383e5e7684e", // as given on CLI
"uuid":"18dd383e5e7684ecee327d3de7d3ff846069d1b2",
"isLeaf":false,
"user":"drh",
"comment":"Merge wideAnnotateUser and jsonWarnings into trunk.",
"timestamp":1330090810,
"parents":[
  // 1st entry is primary parent hash:
  "3a44f95f40a193739aaafc2409f155df43e74a6f",
  // Remaining entries are merged-in branch hashes:
  "86f6e675eb3f8761d70d8b82b052ce2b297fffd2",\
  "dbf4ecf414881c9aad6f4f125dab9762589ef3d7"\
],
"tags":["trunk"],
"files":[{
    "name":"src/diff.c",
    // BLOB hashes, NOT commit hashes:
    "uuid":"78c74c3b37e266f8f7e570d5cf476854b7af9d76",
    "parent":"b1fa7e636cf4e7b6ed20bba2d2680397f80c096a",
    "state":"modified",
    "downloadPath":"/raw/src/diff.c?name=78c74c3b37e266f8f7e570d5cf476854b7af9d76"
  },
  ...]
}
```

The "parents" property lists the parent hashes of the checkin. The
"parent" property of file entries refers to the parent hash of that
file. In the case of a merge there may be essentially an arbitrary
number. The first entry in the list is the "primary" parent. The primary
parent is the parent which was not pulled in via a merge operation. The
ordering of remaining entries is unspecified and may even change between
calls. For example: if, from branch C, we merge in A and B and then
commit, then in the artifact response for that commit the hash of branch
C will be in the first (primary) position, with the hashes for branches A
and B in the following entries (in an unspecified, and possibly
unstable, order).

Note that the "uuid" and "parent" properties of the "files" entries
refer to raw blob hashes, not commit (a.k.a. check-in) hashes. See also
[the UUID vs. Hash discussion][uvh].

<a id="file"></a>
# File Artifacts

Fetches information about file artifacts.

**FIXME:** the content type guessing is currently very primitive, and
may (but I haven't seen this) mis-diagnose some non-binary files as
binary. Fossil doesn't yet have a mechanism for mime-type mappings.

**Status:** implemented 20111020

**Required permissions:** "o"

**Request:** `/json/artifact/FILE_HASH`

**Request options:**

-   `format=(raw|html|none)` (default=none). If set, the contents of the
    artifact are included if they are text, else they are not (JSON does
    not do binary). The "html" flag runs it through the wiki parser. The
    results of doing so are unspecified for non-embedded-doc files. The
    "raw" format means to return the content as-is. "none" is the same
    as not specifying this flag, and elides the content from the
    response.
-   DEPRECATED (use format instead): `includeContent=bool` (=false) (CLI:
    `--content|-c`). If true, the full content of the artifact is returned
    for text-only artifacts (but not for non-text artifacts). As of
    20120713 this option is only inspected if "format" is not specified.

**Response payload example: (CHANGED SIGNIFICANTLY ON 20120713)**

```json
{
"type":"file",
"name":"same name specified as FILE_HASH argument",
"size": 12345, // in bytes, independent of format=...
"parent": "hash of parent file blob. Not set for first generation.",
"checkins":[{
  "name":"src/json_detail.h",
  "timestamp":1319058803,
  "comment":"...",
  "user":"stephan",
  "checkin":"d2c1ae23a90b24f6ca1d7637193a59d5ecf3e680",
  "branch":"json",
  "state":"added|modified|removed"
  },
  ...],
/* The following "content" properties are only set if format=raw|html */
"content": "file contents",
"contentSize": "size of content field, in bytes. Affected by the format option!",
"contentType": "text/plain", /* currently always text/plain */
"contentFormat": "html|raw"
}
```

The "checkins" array lists all checkins which include this file, and a
file might have different names across different branches. The size and
hash, however, are the same across all checkins for a given blob.

<a id="wiki"></a>
# Wiki Artifacts

Returns information about wiki artifacts.

**Status:** implemented 20111020, fixed to return the requested version
(instead of the latest) on 20120302.

**Request:** `/json/artifact/WIKI_HASH`

**Required permissions:** "j"

**Options:**

-   DEPRECATED (use format instead): `bool includeContent` (=false). If
    true then the raw content is returned with the wiki page, else no
    content is returned.\  
    CLI: `--includeContent|-c`
-   The `--format` option works as for
    [`/json/wiki/get`](api-wiki.md#get), and if set then it
    implies the `includeContent` option.

**Response payload example:**

Currently the same as [`/json/wiki/get`](api-wiki.md#get).

[uvh]: ../hashes.md#uvh
