# JSON API: /wiki
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Page List](#list)
* [Fetch a Page](#get)
* [Create or Save Page](#create-save)
* [Wiki Diffs](#diffs)
* [Preview](#preview)
* [Notes and TODOs](#todo)

---

<a id="list"></a>
# Page List

Returns a list of all pages, not including their content (which can be
arbitrarily large).

**Status:** implemented 201109xx

**Required privileges:** "j" or "o"

**Request:** `/json/wiki/list`

**Options:**

-   `bool verbose` (=false) Changes the results to return much more
    information. Added 20120219.
-   `glob=wildcard` string (default=`*`). If set, only page names
    matching the given wildcard are returned. Added 20120325.\  
    CLI: `--glob|-g STRING`
-   `like=SQL LIKE string` (default=`%`). If set, only page names matching
    the given SQL LIKE string are returned. Note that this match is
    case-INsensitive. If both glob and like are given then only one will
    work and which one takes precedence is unspecified. Added 20120325.\  
    CLI: `--like|-l STRING`
-   `invert=bool` (default=false). If set to a true value, the glob/like
    filter has a reverse meaning (pages *not* matching the wildcard are
    returned). Added 20120329.\  
    CLI: `-i/--invert`

**Response payload: format depends on "verbose" option**

Non-verbose mode:

```json
["PageName1",..."PageNameN"]
```

In verbose mode:

```json
[{
"name":"Apache On Windows XP",
"uuid":"a7e68df71b95d35321b9d9aeec3c8068f991926c",
"user":"jeffrimko",
"timestamp":1310227825,
"size":793 /* in bytes */
},...]
```

The verbose-mode output is the same as the [`/json/wiki/get`](#get) output, but
without the content. The size property of each reflects the *byte*
length of the raw (non-HTMLized) page content.

**Potential TODOs:**

-   Allow specifying (in the request) a list/array of names, as opposed
    to listing all pages. The page count is rarely very high, though, so
    an "overload" is very unlikely. (i have one wiki with about 47 pages
    in it.)

<a id="get"></a>
# Fetch a Page

Fetches a single wiki page, including content and significant metadata.

**Status:** implemented 20110922, but response format may change.

**Required privileges:** "j" or "o"

**Request:**

-   GET: `/json/wiki/get?name=PageName`
-   GET: `/json/wiki/get/PageName`
-   POST: `/json/wiki/get` with page name as `POST.payload` or
    `POST.payload.name`.

**Response payload example:**

```json
{
"name": "Fossil",
"uuid": "...hex string...",
"parent": "uuid of parent (not set for first version of page)",
"user": "anonymous",
"timestamp": 1286143975,
"size": 1906, /* In bytes, not UTF8 characters!
                 Affected by format option! */
"content": "..."
}
```

**FIXME:** it's missing the mimetype (that support was added to fossil
after this was implemented).

If given no page to load, or if asked to get a page which does not
exist, an error response is generated (a usage- or resource-not-found
error, respectively).

**Options (via CLI/GET/`POST.payload`):**

- `name=string`. The page to fetch. The latest version is fetched
unless the uuid paramter is supplied (in which case name is ignored). \  
CLI: `--name|-n string`\  
Optionally, the name may be the 4th positional argument/request path element.
- `uuid=string`. Fetches a specific version. The name parameter is
ignored when this is specified.\  
CLI: `--uid|-u string`
- `format=string ("raw"|"html"|"none")` (default="raw"). Specifies the
format of the "content" payload value.\  
CLI: `--format|-f string` \  
The "none" format means to return no content. In that case the size
refers to the same size as the "raw" format.

**TODOs:**

-   Support passing an array of names in the request (and change
    response to return an array).

<a id="create-save"></a>
# Create or Save Page

**Status:** implemented 20110922.

**Required privileges:** "k" (save) or "f" (create)

**Request:**

-   `/json/wiki/create`
-   `/json/wiki/save`

These work only in HTTP mode, not CLI mode. (FIXME: now that we can
simulate POST from a file, these could be used in CLI mode.)

The semantic differences between save and create are:

-   Save will fail if the page doesn't already exist whereas create will
    fail if it does. The createIfNotExists option (described below) can
    be used to create new pages using the save operation.
-   The content property is optional for the create request, whereas it
    is required to be a string for save requests (but it *may* be an
    empty string). This requirement for save is *almost* arbitrary, and
    is intended to prevent accidental erasing of existing page content
    via API misuse.

**Response payload example:**

The same as for [`/json/wiki/get`](#get) but the page content is not
included in the response (only the metadata).

**Request options** (via GET or `POST.payload` object):

-   `name=string` specifies the page name.
-   `content=string` is the body text.\  
    Content is required for save (unless `createIfNotExists` is true *and*
    the page does not exist), optional for create. It *may* be an empty
    string.
-   `mimetype=string` specifies the mimetype for the body, noting any any
    unrecognized/unsupported mimetype is silently treated as
    `text/x-fossil-wiki`.
-   Save (not create) supports a `createIfNotExists` boolean option which
    makes it a functional superset of the create/save features. i.e. it
    will create if needed, else it will update. If createIfNotExists is
    false (the default) then save will fail if given a page name which
    does not refer to an existing page.
-   **TODO:** add `commitMessage` string property. The fossil internals
    don't have a way to do this at the moment (they can as of late 2019).
    Since fossil wiki commits have always had the same default commit message, this is not a
    high-priority addition. See:\  
    [](/doc/trunk/www/fileformat.wiki#wikichng)
-   **Potential TODO:** we *could* optionally also support
    multi-page saving using an array of pages in the request payload:\  
    `[… page objects … ]`



<a id="diffs"></a>
# Wiki Diffs

**Status:** implemented 20120304

**Required privileges:** "h"

**Request:**

-   `/json/wiki/diff[/version1_UUID/version2_UUID]`

**Response payload example:**

```json
{
  "v1":"e32ccdcda59e930c77c1e01cebace5d71253f621",
  "v2":"e15992f475760cdf3a9564d8f88cacb659ab4b07",
  "diff":"@@ -1,4 +1,9 @@...<SNIP>..."
}
```

**Options:**

-   `v1=uuid` and `v2=uuid` specify the two versions to diff, and are
    required parameters. They may optionally be specified as the two
    URL/CLI parameters following the "diff" sub-command/path.

This command does not verify that both UUIDs actually refer to the same
page name, but do verify that they refer to wiki content.

Trivia: passing the same UUIDs to the `/json/diff` command will produce
very different results - that one diffs the manifests of the commits.

**TODOs:**

-   Add options for changing the format of the diff, e.g. side-by-side
    and enabling the HTML markup supported by the main fossil HTML GUI.
-   Potentially do a name comparison to verify that the diff is against
    the same page. That said, when "renaming" pages it might be useful
    to diff two different pages.

<a id="preview"></a>
# Preview

**Status:** implemented 20120310

**Required privileges:** "k" (to limit its use to only those who can
edit wiki pages). This limitation is up for debate/reconsideration.

**Request:**

-   `/json/wiki/preview`

This command wiki-processes arbitrary text sent from the client. To help
curb potential abuse, its use is restricted to those with "k" access
rights.

The `POST.payload` property must be either:

1) A string containing Fossil wiki markup.

2) An Object with a `body` property holding the text to render and a
   `mimetype` property describing the wiki format:
   `text/x-fossil-wiki` (the default), `text/x-markdown`, or
   `text/plain`. Any unknown type is treated as `text/x-fossil-wiki`.

The response payload is a string containing the rendered page. Whether
or not "all HTML" is allowed depends on site-level configuration
options, and that changes how the input is processed.

Note that the links in the generated page are for the HTML interface,
and will not work as-is for arbitrary JSON clients. In order to
integrate the parsed content with JSON-based clients the HTML will
probably need to be post-processed, e.g. using jQuery to fish out the
links and re-map wiki page links to a JSON-capable page handler.


<a id="todo"></a>
# Notes and TODOs

-   When server-parsing the wiki content, the generated
    intra-wiki/intra-site links will only be useful in the context of
    the original fossil UI (or a work-alike), not arbitrary JSON
    client apps.

Potential TODOs:

-   `/wiki/history` analog to the [](/whistory) page.
