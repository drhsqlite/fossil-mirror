# JSON API: /finfo
([&#x2b11;JSON API Index](index.md))

# File Information

**Status:** implemented 2012-something, but output structure is likely
to change.

**Required privileges:** "o"

**Request:** `/json/finfo?name=path/to/file`

Options:

-   `name=string`. Required. Use the absolute name of the file, including
    any directory parts, and without a leading slash. e.g.
    `"path/to/my.c"`.\  
    CLI mode: `--name` or positional argument.
-   `checkin=string`. Only return info related to this specific checkin,
    as opposed to listing all checkins. If set, neither "before" nor
    "after" have any effect.\  
    CLI mode: `--checkin|-ci`
-   `before=DATETIME` only lists checkins from on or before this time.\  
    CLI mode: `--before|-b`
-   `after=DATETIME` only lists checkins from on or after this time.
    Using this option swaps the sort order, to provide reasonable
    behaviour in conjunction with the limit option.\  
    Only one of "before" and "after" may be specified, and if both are
    specified then which one takes precedence is unspecified.\  
    CLI mode: `--after|-a`
-   `limit=integer` limits the output to (at most) the given number of
    associated checkins.\  
    CLI mode: `--limit|-n`

**Response payload example (very likely to change!):**

```json
{
"name":"ajax/i-test/rhino-shell.js",
"checkins":[{
  "checkin":"6b7ddfefbfb871f793378d8f276fe829106ca49b",
  "uuid":"2b735676d55e3d06d670ffbc643e5d3f748b95ea",
  "timestamp":1329514170,
  "user":"viriketo",
  "comment":"<...snip...>",
  "size":6293,
  "state":"added|modified|removed"
  },…]
}
```

**FIXME:** there is a semantic discrepancy between `/json/artifact`'s
`payload.checkins[N].uuid` and this command's.
