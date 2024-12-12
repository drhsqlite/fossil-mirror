# JSON API: /query
([&#x2b11;JSON API Index](index.md))

# SQL Query

**Status:** implemented 20111008

**Required privileges:** "a" or "s"

**Request:** `/json/query`

Potential FIXME: restrict this to queries which return results, as opposed
to those which may modify data.

Options:

-   `sql=string` The SQL code to run. It is expected that it be a SELECT
    statement, but that is not enforced. This parameter may be set as a
    POST.payload property, as the POST.payload itself, GET, or as a
    positional parameter coming after the command name (CLI and HTTP
    modes, though the escaping would be unsightly in HTTP mode).
-   `format=string` (default="o"). "o" specifies that each result row
    should be in the form of key/value pairs (o=object). "a" means each
    row should be an array of values.

**Example request:**

POST to: `/json/query`

```json
{
"authToken": "...",
"payload": {
  "sql": "SELECT * FROM reportfmt",
  "format": "o"
  }
}
```

**Response payload example:** (assuming the above example)


```
{
"columns":[
  "rn",
  "owner",
  "title",
  "mtime",
  "cols",
  "sqlcode"
 ],
  "rows":[
   {
    "rn":1,
    "owner":"drh",
    "title":"All Tickets",
    "mtime":1303870798,
   },
  …
  ]
}
```

The column names are provided in a separate field because their order
is guaranteed to match the order of the query columns, whereas object
key/value pairs might get reordered (typically sorted by key) when
travelling through different JSON implementations. In this manner,
clients can e.g. be sure to render the columns in the proper
(query-specified) order.

When in "array" mode the "rows" results will be an array of arrays. For
example, the above "rows" property would instead look like:

`[ [1, "drh", "All Tickets", 1303870798, … ], … ]`

Note the column *names* are never *guaranteed* to be exactly as they
appear in the SQL *unless* they are qualified with an AS, e.g. `SELECT
foo AS foo...`. When generating reports which need fixed column names, it
is highly recommended to use an AS qualifier for every column, even if
they use the same name as the column. This is the only way to guarantee
that the result column names will be stable. (FYI: that behaviour comes
from sqlite3, not the JSON bits, and this behaviour *has* been known to
change between sqlite3 versions (so this is not just an idle threat of
*potential* future incompatibility).)
