# JSON API: /branch
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Branch List](#list)
* [Create Branch](#create)

---

<a id="list"></a>
# Branch List

**Status:** implemented, at least in draft form, on 20110921.

**Required privileges:** "o"

**Request:** `/json/branch/list`

**Response payload example:**

```json
{
"range":"closed",
"current":"json", /* only when there is a local opened checkout */
"branches":[
  "artifact_description",
  "bch",
  "ben-changes-report",
  "ben-safe-make",
  "ben-security",
  "ben-testing",
  …
]
}
```

*Potential* TODO: add "tip" property which names the most recently
modified branch? (How to get this?)

HTTP GET/`POST.payload` options:

-   `range`: a string in the set ("open", "closed", "all"),
    case-sensitive, but only the first letter is actually evaluated.
    Default="open". Only branches with this state are returned.

CLI mode options (same semantics as HTTP equivalents), must come last on
the CLI:

-   `-r|--range all|closed|open`
-   `-a` (equivalent to `-r all`)
-   `-c` (equivalent to `-r closed`). Only one of `-a`/`-c` may be specified,
    and if both are specified then which one takes precedence is
    unspecified.


<a id="create"></a>
# Create Branch

**Status:** implemented 20111002

**Required privileges:** "w"

**Request:** `/json/branch/create`

**Request options:**

-   `name=string` (REQUIRED) Name of new branch
-   `basis=string` (default=trunk) Name of parent branch to branch from.
-   `bgColor=string` (default=something ugly) In `#RRGGBB` form. (FIXME:
    change the default to use fossil's random bgcolor technique.)
-   `private=bool` (default=false) Determines whether the branch is
    private or not.

**Response payload example:**

```json
{
"name":"my-branch",
"basis":"my-other-branch",
"uuid":"de8115db4ce388ed8d0af666ae7d90e1410be4ca",
"isPrivate":true,
"bgColor":"#ff0000"
}
```

