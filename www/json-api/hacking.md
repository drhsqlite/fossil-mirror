# JSON API: Hacker's Guide
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Before Committing Changes](#before-committing)
* [JSON C API](#json-c-api)
* [Reporting Errors](#reporting-errors)
* [Getting Command Arguments](#command-args)
* [Creating JSON Data](#creating-json)
    * [Creating JSON Values](#creating-json-values)
    * [Converting SQL Query Results to JSON](#query-to-json)

This section will only be of interest to those wanting to work on the
Fossil/JSON code. That said...

If you happen to hack on the code and find something worth noting here
for others, please feel free to expand this section. It will only
improve via feedback from those working on the code.

---

<a id="before-committing"></a>
# Before Committing Changes...

Because this code lives in the trunk, there are certain
guidelines which must be followed before committing any changes:

1.  Read the [checkin preparation list](/doc/trunk/www/checkin.wiki).
2.  Changes to the files `src/json_*.*`, and its related support code
    (e.g. `ajax/*.*`), may be made freely without affecting mainline
    users. Changes to other files, unless they are trivial or made for
    purposes outside the JSON API (e.g. an unrelated bug fix), must be
    reviewed carefully before committing. When in doubt, create a branch
    and post a request for a review.
3.  The Golden Rule is: *do not break the trunk build*.


<a id="json-c-api"></a>
# JSON C API

libcson, the underlying JSON API, is a separate project, included in
fossil in "amalgamation" form: see `extsrc/cson_amalgamation.[ch]`. It has
thorough API docs and a good deal of information is in its wiki:

[](https://fossil.wanderinghorse.net/wikis/cson/)

In particular:

[](https://fossil.wanderinghorse.net/wikis/cson/?page=CsonArchitecture)

gives an overview of its architecture. Occasionally new versions of it
are pulled into the Fossil tree, but other developers generally need not
concern themselves with that.

(Trivia: the cson wiki's back-end is fossil using this very JSON API,
living on top of a custom JavaScript+HTML5 application.)

Only a small handful of low-level fossil routines actually input or
output JSON text (only for reading in POST data and sending the
response). In the C code we work with the higher-level JSON value
abstractions provided by cson (conceptually similar to an XML DOM). All
of the JSON-defined data types are supported, and we can construct JSON
output of near arbitrary complexity with the caveat that *cyclic data
structures are strictly forbidden*, and *will* cause memory corruption,
crashes, double free()'s, or other undefined behaviour. Because JSON
cannot, without client-specific semantic extensions to JSON, represent
cyclic structures, it is not anticipated that this will be a
problem/limitation when generating output for fossil.



<a id="json-commands"></a>
# Architecture of JSON Commands

In order to consolidate CLI/HTTP modes for JSON handling, this code
foregoes fossil's conventional command/path dispatching mechanism. Only
the top-most "json" command/path is dispatched directly by fossil's
core. The disadvantages of this are that we lose fossil's conventional
help text mechanism (which is based on code comments in the
command/path's dispatcher impl) and the ability to write abbreviated
command names in CLI mode ("json" itself may be abbreviated, but not the
subcommands). The advantages are that we can handle CLI/HTTP modes
almost identically (there are a couple of minor differences) by unifying
them under the same callback functions much more easily.

The top-level "json" command/path uses its own dispatching mechanism
which uses either the path (in HTTP mode) or CLI positional arguments to
dispatch commands (stopping at the first "flag option" (e.g. -foo) in
CLI mode). The command handlers are simply callback functions which
return a cson\_value pointer (the C representation of an arbitrary JSON
value), representing the "payload" of the response (or NULL - not all
responses need a payload). On error these callbacks set the internal
JSON error state (detailed in a subsection below) and return NULL. The
top-level dispatcher then creates a response envelope and returns the
"payload" from the command (if any) to the caller. If a callback sets
the error state, the top-level dispatcher takes care to set the error
information in the response envelope. In summary:

-   The top-level dispatchers (`json_page_top()` and `json_cmd_top()`)
    are called by fossil's core when the "json" command/path is called.
    They initialize the JSON-mode global state, dispatch the requested
    command, and handle the creation of the response envelope. They
    prepare all the basic things which the individual subcommands need
    in order to function.
-   The command handlers (most are named `json_page_something()`)
    implement the `fossil_json_f()` callback interface (see
    [`src/json_detail.h`](/finfo/src/json_detail.h)). They are
    responsible for permissions checking, setting any error state, and
    passing back a payload (if needed - not all commands return a
    payload). It is strictly forbidden for these callbacks to produce
    any output on stdout/stderr, and doing so effectively corrupts the
    out-bound JSON and HTTP headers.

There is a wrench in all of that, however: the vast majority of fossil's
commands "fail fast" - they will `exit()` if they encounter an error. To
handle that, the fossil core error reporting routines have been
refactored a small bit to operate differently when we are running in
JSON mode. Instead of the conventional output, they generate a JSON
error response. In HTTP mode they exit with code 0 to avoid causing an
HTTP 500 error, whereas in CLI mode they will exit with a non-0 code.
Those routines still `exit()`, as in the conventional CLI/HTTP modes, but
they will exit differently. Because of this, it is perfectly fine for a
command handler to exit via one of fossil's conventional mechanisms
(e.g. `db_prepare()` can be fatal, and callbacks may call `fossil_panic()`
if they really want to). One exception is `fossil_exit()`, which does
_not_ generate any extra output and will `exit()` the app. In the JSON
API, as a rule of thumb, `fossil_exit()` is only used when we *want* a
failed request to cause an HTTP 500 error, and it is reserved for
allocation errors and similar truly catastrophic failures. That said...
libcson has been hacked to use `fossil_alloc()` and friends for memory
management, and those routines exit on error, so alloc error handling in
the JSON command handler code can afford to be a little lax (the
majority of *potential* errors clients get from the cson API have
allocation failure as their root cause).

As a side-note: the vast majority (if not all) of the cson API calls are
"NULL-safe", meaning that will return an error code (or be a no-op) if
passed NULL arguments. e.g. the following chain of calls will not crash
if the value we're looking for does not exist, is-not-a String (see
`cson_value_get_string()` for important details), or if `myObj` is NULL:

```c
const char * str =
 cson_string_cstr( // get the C-string form of a cson_string
   cson_value_get_string( // get its cson_string form
     cson_object_get(myObj,"foo") // search for key in an Object
   )
 );
```

If `"foo"` is not found in `myObj` (or if `myObj` is NULL) then v will be
NULL, as opposed to stepping on a NULL pointer somewhere in that call
chain.

Note that all cson JSON values except Arrays and Objects are *immutable*
- you cannot change a string's or number's value, for example. They also
use reference counting to manage ownership, as documented and
demonstrated on this page:

[](https://fossil.wanderinghorse.net/wikis/cson/?page=TipsAndTricks)

In short, after creating a new value you must eventually *either* add it
to a container (Object or Array) to transfer ownership *or* call
`cson_value_free()` to clean it up (exception: the Fossil/JSON command
callbacks *return* a value to transfer ownership to the dispatcher).
Sometimes it's more complex than that, but not normally. Any given value
may legally be stored in any number of containers (or multiple times
within one container), as long as *no cycles* are introduced (cycles
*will* cause undefined behaviour). Ownership is shared using reference
counting and the value will eventually be freed up when its last
remaining reference is freed (e.g. when the last container holding it is
cleaned up). For many examples of using cson in the context of fossil,
see the existing `json_page_xxx()` functions in `json_*.c`.

<a id="reporting-errors"></a>
# Reporting Errors

To report an error from a command callback, one abstractly needs to:

-   Set g.json.resultCode to one of the `FSL_JSON_E_xxx` values
    (defined in [`src/json_detail.h`](/finfo/src/json_detail.h)).
-   *Optionally* set `g.zErrMsg` to contain the (dynamically-allocated!)
    error string to be sent to the client. If no error string is set
    then a standard/generic string is used for the given error code.
-   Clean up any resources created so far by the handler.
-   Return NULL. If it returns non-NULL, the dispatcher will destroy the
    value and not include it in the error response.

That normally looks something like this:

```
if(!g.perm.Read){
  json_set_err(FSL_JSON_E_DENIED, "Requires 'o' permissions.");
  return NULL;
}
```

`json_set_err()` is a variadic printf-like function, and can use the
printf extensions supported by mprintf() and friends (e.g. `%Q` and `%q`)
(but they are normally not needed in the context of JSON). If the error
string is NULL or empty then `json_err_cstr(errorCode)` is used to fetch
the standard/generic error string for the given code.

When control returns to the top-level dispatching function it will check
`g.json.resultCode` and, if it is not 0, create an error response using
the `g.json.resultCode` and `g.zErrMsg` to construct the response's
`resultCode` and `resultText` properties.

If a function wants to output an error and exit by itself, as opposed
to returning to the dispatcher, then it must behave slightly
differently.  See the docs for `json_err()` (in
[`src/json.c`](/finfo/src/json.c)) for details, and search that file
for various examples of its usage. It is also used by fossil's core
error-reporting APIs, e.g. `fossil_panic()` (defined in [`src/main.c`](/finfo/src/main.c)).
That said, it would be "highly unusual" for a callback to need to do
this - it is *far* simpler (and more consistent/reliable) to set the
error state and return to the dispatcher.

<a id="command-args"></a>
# Getting Command Arguments

Positional parameters can be fetched usinig `json_command_arg(N)`, where
N is the argument position, with position 0 being the "json"
command/path. In CLI mode positional arguments have their obvious
meaning. In HTTP mode the request path (or the "command" request
property) is used to build up the "command path" instead. For example:

CLI: `fossil json a b c`

HTTP: `/json/a/b/c`

HTTP POST or CLI with `--json-input`: /json with POSTed envelope
`{"command": "a/b/c" …}`

Those will have identical "command paths," and `json_command_path(2)`
would return the "b" part.

Caveat: a limitation of this support is that all CLI flags must come
*after* all *non-flag* positional arguments (e.g. file names or
subcommand names). Any argument starting with a dash ("-") is considered
by this code to be a potential "flag" argument, and all arguments after
it are ignored (because the generic handling cannot know if a flag
requires an argument, which changes how the rest of the arguments need
to be interpreted).

To get named parameters, there are several approaches (plus some special
cases). Named parameters can normally come from any of the following
sources:

-   CLI arguments, e.g. `--foo bar`
-   GET parameters: `/json/...?foo=bar`
-   Properties of the POST envelope
-   Properties of the `POST.payload` object (if any).

To try to simplify the guessing process the API has a number of
functions which behave ever so slightly differently. A summary:

-   `json_getenv()` and `json_getenv_TYPE()` search the so-called "JSON
    environment," which is a superset of the GET/POST/`POST.payload` (if
    `POST.payload` is-a Object).
-   `json_find_option_TYPE()`: searches the CLI args (only when in CLI
    mode) and the JSON environment.
-   The use of fossil's `P()` and `PD()` macros is discouraged in JSON
    callbacks because they can only handle String data from the CLI or
    GET parameters (not POST/`POST.payload`). (Note that `P()` and `PD()`
    *normally* also handle POSTed keys, but they only "see" values
    posted as form-urlencoded fields, and not JSON format.)
-   `find_option()` (from `src/main.c`) "should" also be avoided in
    JSON API handlers because it removes flag from the g.argv
    arguments list. That said, the JSON API does use `find_option()` in
    several of its option-finding convenience wrappers.

For example code: the existing command callbacks demonstrate all kinds
of uses and the various styles of parameter/option inspection. Check out
any of the functions named `json_page_SOMETHING()`.

<a href="creating-json"></a>
# Creating JSON Data

<a href="creating-json-values"></a>
## Creating JSON Values

cson has a fairly rich API for creating and manipulating the various
JSON-defined value types. For a detailed overview and demonstration I
recommend reading:

[](https://fossil.wanderinghorse.net/wikis/cson/?page=HowTo)

That said, the Fossil/JSON API has several convenience wrappers to save
a few bytes of typing:

-   `json_new_string("foo")` is easier to use than
    `cson_value_new_string("foo", 3)`, and
    `json_new_string_f("%s","foo")` is more flexible.
-   `json_new_int()` is easier to type than `cson_value_new_integer()`.
-   `cson_output_Blob()` and `cson_parse_Blob()` can write/read JSON
    to/from fossil `Blob`-type objects.

It also provides several lower-level JSON features which aren't of
general utility but provide necessary functionality for some of the
framework-level code (e.g. `cson_data_dest_cgi()`), which is only used
by the deepest of the JSON internals).


<a href="query-to-json"></a>
## Converting SQL Query Results to JSON

The `cson_sqlite3_xxx()` family of functions convert `sqlite3_stmt` rows
to Arrays or Objects, or convert single columns to a JSON-compatible
form. See `json_stmt_to_array_of_obj()`,
`json_stmt_to_array_of_array()` (both in `src/json.c`), and
`cson_sqlite3_column_to_value()` and friends (in
`extsrc/cson_amalgamation.h`). They work in an intuitive way for numeric
types, but they optimistically/naively *assume* that any fields of type
TEXT or BLOB are actually UTF8 data, and treat them as such. cson's
string class only handles UTF8 data and it is semantically illegal to
feed them anything but UTF8. Violating this will likely result in
down-stream errors (e.g. when emitting the JSON string output). **The
moral of this story is:** *do not use these APIs to fetch binary data*.
JSON doesn't do binary and the `cson_string` class does not
protect itself against clients feeding it non-UTF8 data.

Here's a basic example of using these features:

```c
Stmt q = empty_Stmt;
cson_value * rows = NULL;
db_prepare(&q, "SELECT a AS a, b AS b, c AS c FROM foo");
rows = json_stmt_to_array_of_obj( &sql, NULL );
db_finalize(&q);
// side note: if db_prepare()/finalize() fail (==they exit())
// then a JSON-format error reponse will be generated.
```

On success (and if there were results), `rows` is now an Array value,
each entry of which contains an Object containing the fields (key/value
pairs) of each row. `json_stmt_to_array_of_array()` returns each row
as an Array containing the column values (with no column name
information).

**Note the seemingly superfluous use of the "AS" clause in the above
SQL.** Having them is actually significant! If a query does *not* use AS
clauses, the row names returned by the db driver *might* be different
than they appear in the query (this is documented behaviour of sqlite3).
Because the JSON API needs to return stable field names, we need to use
AS clauses to be guaranteed that the db driver will return the column
names we want. Note that the AS clause is often used to translate column
names into something more JSON-conventional or user-friendly, e.g.
"SELECT cap AS capabilities...". Alternately, we can convert the
individual `sqlite3_stmt` column values to JSON using
`cson_sqlite3_column_to_value()`, without referring directly to the
db-reported column name.
