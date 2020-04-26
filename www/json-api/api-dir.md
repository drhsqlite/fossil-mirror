# JSON API: /dir
([&#x2b11;JSON API Index](index.md))

# Directory Listing

**Status:** implemented 20120316

**Required privileges:** "o". Was "h" prior to 20120720, but the HTML
version of /dir changed permissions and this API was modified to match
it.

**Request:** `/json/dir`

Options:

-   `checkin=commit` (use "tip" for the latest). If checkin is not set
    then all files from all versions of the tree are returned (but only
    once per file - not with complete version info for each file in all
    branches).\  
    CLI: `--checkin|-ci CHECKIN`
-   `name=subdirectory` name. To fetch the root directory, don't pass this
    option, or use an empty value or "/".\  
    CLI: use `--name|-n NAME` or pass it as the first argument after
    the `dir` subcommand.

**Response payload example:**

```json
{
 "name":"/", /* dir name */
 "uuid":"ac6366218035ed62254c8d458f30801273e5d4fc",
 "checkin":"tip",
 "entries":[{
  "name": "ajax", /* file name/dir name fragment */
  "isDir": true, /* only set for directories */
  /* The following properties are ONLY set if
   the 'checkin' parameter is provided.
  */
  "uuid": "..." /*only for files, not dirs*/,
  "size": number,
  "timestamp": number
 },...]
}
```

The checkin property is only set if the request includes it. The
entry-specific uuid and size properties (e.g. `entries[0].uuid`) are
only set if the checkin request property is set and they refer to the
latest version of that file for the given checkin. The `isDir` property is
only set on directory entries.

This command does not recurse into subdirectories, though it "should be"
simple enough to add the option to do so.
