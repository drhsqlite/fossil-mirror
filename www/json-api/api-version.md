# JSON API: /version
([&#x2b11;JSON API Index](index.md))

# Version (a.k.a. HAI)

**Status:** implemented

**Required privileges:** none

**Requests:**

-   `/json/version`
-   `/json/HAI` (alias borrowed from LOLCATZ jargon)

**Response payload example:**

```json
{
"manifestUuid":"20ff808f9809541d2eca6c49a17d5cbd16e1b93f",
"manifestVersion":"[20ff808f98]",
"manifestDate":"2011-09-09 16:49:23",
"manifestYear":"2011",
"releaseVersion":"1.19",
"releaseVersionNumber":119,
"jsonApiVersion": "YYYYMMDD" // added 20120409
}
```

Those particular payload fields were chosen only because they're defined
in `VERSION.h`. We may want to add other information, but nothing comes to
mind at this time.

