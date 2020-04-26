# JSON API: Misc. APIs
([&#x2b11;JSON API Index](index.md))

Some operations simply don't fit into a specific category (well, none
except "misc")...

Jump to:

* [Global State ("g")](#g)
* [Rebuild Repository](#rebuild)
* [Result Code Descriptions](#result-codes)

---

<a id="g"></a>
# Global State ("g")

Fossil's internal state is maintained in a global object called "g". And
thus this command is named "g"...

**Status:** implemented 20111009

**Required permissions:** "a" or "s"

**Request:** `/json/g`

**Response payload example:** this is a debugging-only request, and has
no stable response payload structure. The result object contains all
kinds of details gleaned from the fossil environment.

Easter egg: this output can be added to ANY response by passing the
`debugFossilG` boolean in the POST envelope or GET parameters, or as the
`--json-debug-g` flag in CLI mode. This requires admin or setup
privileges, though, and it is silently ignored for other users.


<a id="rebuild"></a>
# Rebuild Repository

This request does the same as the "fossil rebuild" command, rebuilding
the repo-internal structure. This is often required after upgrading the
fossil binary on a system. There "are very probably" cases where calling
this over HTTP will not work (e.g. if the user table has changed enough
that the access rights cannot be validated without a rebuild, i.e. a
chicken/egg scenario). Another consideration is that this request can
take a long time to run - rebuilding the fossil repo on my laptop takes
about 21 seconds, which is likely longer than the timeout set on an XHR
request, meaning that the rebuild transaction will fail. It can safely
be run in CLI mode, where timeouts are not normally a concern. As a
preliminary benchmark: rebuilding the fossil repo (as of late 2011)
takes just over 21 seconds on my 32-bit 1.6GHz netbook. That said, most
repos are much smaller and rebuild in under a few seconds.

**Status:** implemented 20110929

**Required privileges:** "a"

**Request:** `/json/rebuild`

Requires admin access rights.

**Response payload:** none (response envelope `resultCode` reports failure).
Potential TODO: return the amount of time it took to rebuild.


<a id="result-codes"></a>
# Result Code Descriptions

This request returns the full list of result codes documented for
Fossil's JSON API. Each result code is returned as an object containing
metadata about the result code.

**Status:** implemented 20111006

**Required permissions:** none

**Request:** `/json/resultCodes`

**Response payload example:**

```json
[{
  "resultCode":"FOSSIL-1000",
  "cSymbol":"FSL_JSON_E_GENERIC",
  "number":1000,
  "description":"Generic error"
 },
 … many more objects with the same structure …
]
```


