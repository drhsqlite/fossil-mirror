# JSON API: /diff
([&#x2b11;JSON API Index](index.md))

# Diffs

**Status:** implemented 20111007

**Required permissions:** "o"

**Request:** `/json/diff[/version1[/version2]]`

**Request options:**

-   `v1=string` Is the "from" version. It may optionally be the first
    positional parameter/path element after the command name.
-   `v2=string` Is the "to" version. It may optionally be the first
    positional parameter/path element after the v1 part.
-   `context=integer` (default=unspecified) Defines the number of context
    lines to display in the diff.\  
    CLI: `--context|-c`
-   `sbs=bool` (default=false) Generates "side-by-side" diffs, but their
    utility in JSON mode is questionable.
-   `html=bool` (default=false) causes the output to be marked up with
    HTML in the same manner as it is in the HTML interface.

**Response payload example:**

```json
{
"from":"7a83a5cbd0424cefa2cdc001de60153aede530f5",
"to":"96920e7c04746c55ceed6e24fc82879886cb8197",
"diff":"@@ -1,7 +1,7 @@\\n-C factored\\\\sout..."
}
```

TODOs:

-   Unlike the standard diff command, which apparently requires a commit
    hash, this one diffs individual file versions. If a commit hash is
    provided, a diff of the manifests is returned. (That should be
    considered a bug - we should return a combined diff in that case.)
-   If hashes from two different types of artifacts are given, results
    are unspecified. Garbage in, garbage out, and all that.
-   For file diffs, add the file name(s) to the response payload.
