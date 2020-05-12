# JSON API: /stat
([&#x2b11;JSON API Index](index.md))

# Repository Stats

**Status:** implemented

**Required privileges:** "o"

**Request:** `/json/stat`

**Response payload example:** (fields marked with `*` are omitted in
"brief" mode)

```json
{
"projectName":"Fossil",
"projectDescription":"Fossil SCM", /* added 20120217 */
"repositorySize":24464384,
* "blobCount":13612,
* "deltaCount":9348,
* "uncompressedArtifactSize":589205834,
* "averageArtifactSize":43292,
* "maxArtifactSize":4620758,
* "compressionRatio":"24:1",
* "checkinCount":3150,
* "fileCount":456,
* "wikiPageCount":23,
* "ticketCount":940,
"ageDays":1512,
"ageYears":4.139744,
"projectCode":"25d3a4b83202c0d616a5ed17334f180dac4434dc",
"compiler":"gcc-4.1.2 20080704 (Red Hat 4.1.2-50)",
"sqlite":{
  "version":"2011-09-04 01:27:00 [6b657ae750] (3.7.8)",
  "pageCount":23891,
  "pageSize":1024,
  "freeList":2705,
  "encoding":"UTF-8",
  "journalMode":"delete"
}
}
```

**Options:**

-   "Full detail" mode:\  
    **HTTP/payload parameter:** full=bool\  
    **CLI MODE:** -f|--full with no value.\  
    If true then all properties are included, else certain properties
    are omitted from the payload (because they take a relatively long
    time to calculate).\
    **TODO:** rename this to verbose, for consistency.\  
    **Default=false**. *This is in contrast to the HTML interface*,
    which defaults to full detail mode. Testing shows stat to have a
    relatively high per-call cost/run time, so it defaults
    to "brief" mode by default. Full-detail mode can, on slow hardware,
    take half a minute to respond, whereas non-full mode takes well
    under one second.
