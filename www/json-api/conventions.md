# JSON API: General Conventions
([&#x2b11;JSON API Index](index.md))

Jump to:

* [JSON Property Naming](#property-names)
* [HTTP GET Requests](#http-get)
* [HTTP POST Requests](#http-post)
    * [POST Request Envelope](#request-envelope)
* [Request Parameter Data Types](#request-param-types)
* [Response Envelope](#response-envelope)
* [HTTP Response Headers](#http-response-header)
* [CLI vs. HTTP Mode](#cli-vs-http)
* [Simulating POSTed data](#simulating-post-data)
* [Indentation/Formatting of JSON Output](#json-indentation)
* [JSONP](#jsonp)
* [API Result Codes](#result-codes)

---

<a id="property-names"></a>
# JSON Property Naming

Since most JSON usage conventionally happens in JavaScript
environments, this API has (without an explicit decision ever having
been made) adopted the ubiquitous JavaScript convention of
`camelCaseWithStartingLower` for naming properties in JSON objects.

<a id="http-get"></a>
# HTTP GET Requests

Many (if not most) requests can be made via simple GET requests, e.g. we
*could* use any of the following patterns for a hypothetical JSON-format
timeline:

- `https://..../timeline/json`
- `/timeline?format=json`
- `/timeline?json=1`
- `/timeline.json`
- `/json/timeline?...options...`

The API settled on the `/json/...` convention, primarily because it
simplifies dispatching and argument-handling logic compared to the
`/[.../]foo.json` approach. Using `/json/...` allows us to unify that
logic for all JSON sub-commands, for both CLI and HTTP modes.

<a id="http-post"></a>
# HTTP Post Requests

Certain requests, mainly things like editing checkin messages and
committing new files entirely, require POST data. This is fundamentally
very simple to do - clients post plain/unencoded JSON using a common
wrapper envelope which contains the request-specific data to submit as
well as some request-independent information (like authentication data).

<a id="request-envelope"></a>
## POST Request Envelope

POST requests are sent to the same URL as their GET counterpart (if any,
else their own path), and are sent as plain-text/unencoded JSON wrapped
in a common request envelope with the following properties:

- `requestId`: Optional arbitrary JSON value, not used by fossil, but
  returned as-is in responses.
- `command`: Provides a secondary mechanism for specifying which JSON
  command should be run. A request path of /json/foo/bar is equivalent
  to a request with path=/json and command=foo/bar. Note that subpaths
  do not work this way. e.g. path=/json/foo, command=bar will not
  work, but path=/json, command=foo/bar will.  This option is
  particularly useful when generating JSON for piping in to CLI mode,
  but it also has some response-dispatching uses on the client side.
- `authToken`: Authentication token. Created by a login
  request. Determines what access rights the user has, and any given
  request may require specific rights. In principle this is required
  by any request which needs non-guest privileges, but cookie-aware
  clients do not manually need to track this (it is managed as a
  cookie by the agent/browser).
    - Note that when accessing fossil over a local server instance
    which was started with the `--localauth` flag, the `authToken`
    will be ignored and need not be sent with any requests. The user
    will automatically be given full privileges, as if they were
    using CLI mode.
- `payload`: Command-specific parameters. Most can optionally come in
  via GET parameters, but those taking complex structures expect them
  to be placed here.
- `indent`: Optionally specifies indentation for the output. 0=no
  indention. 1=a single TAB character for each level of
  indentation. >1 means that many spaces per level. e.g. indent=7
  means to indent 7 spaces per object/array depth level. cson also
  supports other flags for fine-tuning the output spacing, and adding
  them to this interface might be interesting at some
  point. e.g. whether or not to add a newline to the output.  CLI mode
  adds extra indentation by default, whereas CGI/server modes produce
  unindented output by default.
- `jsonp`: Optional String (client function name). Requests which
  include this will be returned with `Content-Type
  application/javascript` and will be wrapped up in a function call
  using the given name. e.g. if `jsonp=foo` then the result would look like:\  
`foo( {...the response envelope...} )`

The API allows most of those (normally all but the payload) to come in
as either GET parameters or properties of the top-level POSTed request
JSON envelope, with GET taking priority over POST. (Reminder to self: we
could potentially also use values from cookies. Fossil currently only
uses 1 cookie (the login token), and I'd prefer to keep it that way.)

POST requests without such an envelope will be rejected, generating a
Fossil/JSON error response (as opposed to an HTTP error response). GET
requests, by definition, never have an envelope.

POSTed client requests *must* send a Content-Type header of either
`application/json` , `application/javascript`, or `text/plain`, or the
JSON-handling code will never see the POST data. The POST handler
optimistically assumes that type `text/plain` "might be JSON", since
`application/json` is a newer convention which many existing clients
do not use (as of the time these docs were written, back in 2011).

## POST Envelope vs. `POST.payload`


When this document refers to POST data, it is referring to top-level
object in JSON-format POSTed input data. When we say `POST.payload` we
refer to the "payload" property of the POST data. While fossil's core
handles *form-urlencoded* POST data, if such data is sent in then
parsing it as JSON cannot succeed, which means that (at worst) the
JSON-mode bits will "not see" any POST data. Data POSTed to the JSON API
must be sent non-form-urlencoded (i.e. as plain text).

Framework-level configuration options are always set via the top-level
POST envelope object or GET parameters. Request-specific options are set
either in `POST.payload` or GET parameters (though the former is required
in some cases). Here is an example which demonstrates the possibly
not-so-obvious difference between the two types of options (framework
vs. request-specific):

```json
{
"requestId":"my request", // standard envelope property (optional)
"command": "timeline/wiki", // also standard
"indent":2, // output indention is a framework-level option
"payload":{ // *anything* in the payload is request-specific
   "limit":1
}
}
```

When a given parameter is set in two places, e.g. GET and POST, or
POST-from-a-file and CLI parameters, which one takes precedence
depends on the concrete command handler (and may be unspecified). Most
will give precedence to CLI and GET parameters, but POSTed values are
technically preferred for non-string data because no additional "type
guessing" or string-to-whatever conversion has to be made (GET/CLI
parameters are *always* strings, even if they look like a number or
boolean).


<a id="request-param-types"></a>
# Request Parameter Data Types

When parameters are sent in the form of GET or CLI arguments, they are
inherently strings. When they come in from JSON they keep their full
type (boolean, number, etc.). All parameters in this API specify what
*type* (or types) they must (or may) be. For strings, there is no
internal conversion/interpretation needed for GET- or CLI-provided
parameters, but for other types we sometimes have to convert strings to
other atomic types. This section describes how those string-to-whatever
conversions behave.

No higher-level constructs, e.g. JSON **arrays** or **objects**, are
accepted in string form. Such parameters must be set in the POST
envelope or payload, as specified by the specific API.

This API does not currently use any **floating-point** parameters, but
does return floating-point results in a couple of places.

For **integer** parameters we use a conventional string-to-int algorithm
and assume base 10 (analog to atoi(3)). The API may err on the side of
usability when given technically invalid values. e.g. "123abc" will
likely be interpreted as the integer 123. No APIs currently rely on
integer parameters with more than 32 bits (signedness is call-dependent
but few, if any, use negative values).

**Boolean** parameters are a bit schizophrenic...

In **CLI mode**, boolean flags do not have a value, per se, and thus
require no string-to-bool conversion. e.g.
`fossil foo -aBoolOpt -non-bool-opt value`.

Those which arrive as strings via **GET parameters** treat any of the
following as true: a string starting with a character in the set
`[1-9tT]`. All other string values are considered to be false for this
purpose.

Those which are part of the **POST data** are normally (but not always -
it depends on the exact context) evaluated as the equivalent of
JavaScript booleans. e.g. if we have `POST.envelope.foo="f"`, and evaluate
it as a JSON boolean (as opposed to a string-to-bool conversion), the
result will be true because the underlying JSON API follows JavaScript
semantics for any-type-to-bool conversions. As long as clients always
send "proper" booleans in their POST data, the difference between
GET/CLI-provided booleans should never concern them.

TODO: consider changing the GET-value-to-bool semantics to match the JS
semantics, for consistency (within the JSON API at least, but that might
cause inconsistencies vis-a-vis the HTML interface).

<a id="response-envelope"></a>
# Response Envelope

Every response comes in the form of an HTTP response or (in CLI mode)
JSON sent to stdout. The body of the response is a JSON object following
a common envelope format. The envelope has the following properties:


- `fossil`: Fossil server version string. This property is basically
  "the official response envelope marker" - if it is set, clients can
  "probably safely assume" that the object indeed came from one of the
  Fossil/JSON APIs. This API never creates responses which do not
  contain this property.
- `requestId`: Only set if the request contained it, and then it is
  echoed back to the caller as-is. This can be used to determine
  (client-side) which request a given response is coming in for
  (assuming multiple asynchronous requests are pending). In practice
  this generally isn’t needed because response handling tends to be
  done by closures associated with the original request object (at
  least in JavaScript code). In languages without closures it might
  have some use. It may be any legal JSON value - it need not be
  confined to a string or number.
- `resultCode`: Standardized result code string in the form
  `FOSSIL-####`. Only error responses contain a `resultCode`.
- `resultText`: Possibly a descriptive string, possibly
  empty. Supplements the resultCode, but can also be set on success
  responses (but normally isn't). Clients must not rely on any
  specific values being set here.
- `payload`: Request-specific response payload (data type/structure is
  request-specific).  The payload is never set for error responses,
  only for success responses (and only those which actually have a
  payload - not all do).
- `timestamp`: Response timestamp (GMT Unix Epoch). We use seconds
  precision because I did not know at the time that Fossil actually
  records millisecond precision.
- `payloadVersion`: Not initially needed, but reserved for future use
  in maintaining version compatibility when the format of a given
  response type's payload changes. If needed, the "first version"
  value is assumed to be 0, for semantic [near-]compatibility with the
  undefined value clients see when this property is not set.
- `command`: Normalized form of the command being run. It consists of
  the "command" (non-argument) parts of the request path (or CLI
  positional arguments), excluding the initial "/json/" part. e.g. the
  "command" part of "/json/timeline/checkin?a=b" (CLI: json timeline
  checkin...)  is "timeline/checkin" (both in CLI and HTTP modes).
- `apiVersion`: Not yet used, but reserved for a numeric value which
  represents the JSON API's version (which can be used to determine if
  it has a given feature or not). This will not be implemented until
  it's needed.
- `warnings`: Reserved for future use as a standard place to put
  non-fatal warnings in responses. Will be an array but the warning
  structure/type is not yet specified. Intended primarily as a
  debugging tool, and will "probably not" become part of the public
  client interface.
- `g`: Fossil administrators (those with the "a" or "s" permissions)
  may set the `debugFossilG` boolean request parameter (CLI:
  `--json-debug-g`) to enable this property for any given response. It
  contains a good deal of the server-side internal state at the time
  the response was generated, which is often useful in debuggering
  problems. Trivia: it is called "g" because that's the name of
  fossil's internal global state object.
- `procTimeMs`: For debugging only - generic clients must not rely on
  this property. Contains the number of milliseconds the JSON command
  processor needed to dispatch and process the command. TODO: move the
  timer into the fossil core so that we can generically time its
  responses and include the startup overhead in the time calculation.



<a id="http-response-header"></a>
# HTTP Response Headers

The Content-Type HTTP header of a response will be either
application/json, application/javascript, or text/plain, depending on
whether or not we are in JSONP mode or (failing that) the contents of
the "Accept" header sent in the request. The response type will be
text/plain if it cannot figure out what to do. The response's
Content-Type header *may* contain additional metadata, e.g. it might
look like: application/json; charset=utf-8

Apropos UTF-8: note that JSON is, by definition, Unicode and recommends
UTF-8 encoding (which is what we use). That means if your console cannot
handle UTF-8 then using this API in CLI mode might (depending on the
content) render garbage on your screen.


<a id="cli-vs-http"></a>
# CLI vs. HTTP Mode

CLI (command-line interface) and HTTP modes (CGI and standalone server)
are consolidated in the same implementations and behave essentially
identically, with only minor exceptions.

An HTTP path of `/json/foo` translates to the CLI command `fossil json
foo`. CLI mode takes options in the fossil-convention forms (e.g. `--foo 3`
or `-f 3`) whereas HTTP mode takes them via GET/POST data (e.g. `?foo=1`).
(Note that per long-standing fossil convention CLI parameters taking a
value do not use an equal sign before the value!)

For example:

-   HTTP: `/json/timeline/wiki?after=2011-09-01&limit=3`
-   CLI: `fossil json timeline wiki --after 2011-09-01 --limit 3`

Some commands may only work in one mode or the other (for various
reasons). In CLI mode the user automatically has full setup/admin
access.

In HTTP mode, request-specific options can also be specified in the
`POST.payload` data, and doing so actually has an advantage over
specifying them as URL parameters: posting JSON data retains the full
type information of the values, whereas GET-style parameters are always
strings and must be explicitly type-checked/converted (which may produce
unpredictable results when given invalid input). That said, oftentimes
it is more convenient to pass the options via URL parameters, rather
than generate the request envelope and payload required by POST
requests, and the JSON API makes some extra effort to treat GET-style
parameters type-equivalent to their POST counterparts. If a property
appears in both GET and `POST.payload`, GET-style parameters *typically*
take precedence over `POST.payload` by long-standing convention (=="PHP
does it this way by default").

(That is, however, subject to eventual reversal because of the
stronger type safety provided by POSTed JSON. Philosophically
speaking, though, GET *should* take precedence, in the same way that
CLI-provided options conventionally override app-configuration-level
options.)

One notable functional difference between CLI and HTTP modes is that in
CLI mode error responses *might* be accompanied by a non-0 exit status
(they "should" always be, but there might be cases where that does not
yet happen) whereas in HTTP mode we always try to exit with code 0 to
avoid generating an HTTP 500 ("internal server error"), which could keep
the JSON response from being delivered. The JSON code only intentionally
allows an HTTP 500 when there is a serious internal error like
allocation or assertion failure. HTTP clients are expected to catch
errors by evaluating the response object, not the HTTP result code.

<a id="simulating-post-data"></a>
# Simulating POSTed data

We have a mechanism to feed request data to CLI mode via
files (simulating POSTed data), as demonstrated in this example:

```console
$ cat in.json
{ "command": "timeline/wiki", "indent":2, "payload":{"limit":1}}
$ fossil json --json-input in.json # use filename - for stdin
```

The above is equivalent to:

```console
$ echo '{"indent":2, "payload":{"limit":1}}' \
 | fossil json timeline wiki --json-input -
```

Note that the "command" JSON parameter is only checked when no json
subcommand is provided on the CLI or via the HTTP request path. Thus we
cannot pass the CLI args "json timeline" in conjunction with a "command"
string of "wiki" this way.

***HOWEVER...***

Much of the existing JSON code was written before the `--json-input`
option was possible. Because of this, there might be some
"misinteractions" when providing request-specific options via *both*
CLI options and simulated POST data. Those cases will eventually be
ironed out (with CLI options taking precedence). Until then, when
"POSTing" data in CLI mode, for consistent/predictible results always
provide any options via the JSON request data, not CLI arguments. That
said, there "should not" be any blatant incompatibilities, but some
routines will prefer `POST.payload` over CLI/GET arguments, so there
are some minor inconsistencies across various commands with regards to
which source (POST/GET/CLI) takes precedence for a given option. The
precedence "should always be the same," but currently cannot be due to
core fossil implementation details (the internal consolidation of
GET/CLI/POST vars into a single set).


<a id="json-indentation"></a>
# Indentation/Formatting of JSON Output

CLI mode accepts the `--indent|-I #` option to set the indention level
and HTTP mode accepts `indent=#` as a GET/POST parameter. The semantics
of the indention level are derived from the underlying JSON library and
have the following meanings: 0 (zero) or less disables all superfluous
indentation (this is the default in HTTP mode). A value of 1 uses 1 hard
TAB character (ASCII 0x09) per level of indention (the default in CLI
mode). Values greater than 1 use that many whitespaces (ASCII 32d) per
level of indention. e.g. a value of 7 uses 7 spaces per level of
indention. There is no way to specify one whitespace per level, but if
you *really* want one whitespace instead of one tab (same data size) you
can filter the output to globally replace ASCII 9dec (TAB) with ASCII
32dec (space). Because JSON string values *never* contain hard tabs
(they are represented by `\t`) there is no chance that such a global
replacement will corrupt JSON string contents - only the formatting will
be affected.

Potential TODO: because extraneous indention "could potentially" be used
as a form of DoS, the option *might* be subject to later removal from HTTP
mode (in CLI it's fine).

In HTTP mode no trailing newline is added to the output, whereas in CLI
mode one is normally appended (exception: in JSONP mode no newline is
appended, to (rather pedantically and arbitraily) allow the client to
add a semicolon at the end if he likes). There is currently no option to
control the newline behaviour, but the underlying JSON code supports
this option, so adding it to this API is just a matter of adding the
CLI/HTTP args for it.

Pedantic note: internally the indention level is stored as a single
byte, so giving large indention values will cause harmless numeric
overflow (with only cosmetic effects), meaning, e.g., 257 will overflow
to the value 1.

Potential TODO: consider changing cson's indention mechanism to use a
*signed* number, using negative values for tabs and positive for
whitespace count (or the other way around). This would require more
doc changes than code changes :/.


<a id="jsonp"></a>
# JSONP

The API supports JSONP-style output. The caller specifies the callback
name and the JSON response will be wrapped in a function call to that
name. For HTTP mode pass the `jsonp=string` option (via GET or POST
envelope) and for CLI use `--jsonp string`.

For example, if we pass the JSONP name `myCallback` then a response will
look like:

```js
myCallback({...response...})
```

Note that fossil does not evaluate the callback name itself, other than
to verify that it is-a string, so "garbage in, garbage out," and all
that. (Remember that CLI and GET parameters are *always* strings, even
if they *look* like numbers.)


<a id="result-codes"></a>
# API Result Codes

Result codes are strings which tell the client whether or not a given
API call succeeded or failed, and if it failed *perhaps* some hint as to
why it failed.

The result code is available via the resultCode property of every
*error* response envelope. Since having a result code value for success
responses is somewhat redundant, success responses contain no resultCode
property. In practice this simplifies error checking on the client side.

The codes are strings in the form `FOSSIL-####`, where `####` is a
4-digit integral number, left-padded with zeros. The numbers follow
these conventions:

-   The number 0000 is reserved for the "not an error" (OK) case. Since
    success responses do not contain a result code, clients won't see
    this value (except in documentation).
-   All numbers with a leading 0 are reserved for *potential* future use
    in reporting non-fatal warnings.
-   Despite *possibly* having leading zeros, the numbers are decimal,
    not octal. Script code which uses eval() or similar to produce
    integers from them may need to take that into account.
-   The 1000ths and 100ths places of the number describe the general
    category of the error, e.g. authentication- vs. database- vs. usage
    errors. The 100ths place is more specific than the 1000ths place,
    allowing two levels of sub-categorization (which "should be enough"
    for this purpose). This separation allows the server administrator
    to configure the level of granularity of error reporting. e.g. some
    admins consider error messages to be security-relevant and like to
    "dumb them down" on their way to the client, whereas developers
    normally want to see very specific error codes when tracking down a
    problem. We can offer a configuration option to "dumb down" error
    codes to their generic category by simply doing a modulo 100
    (or 1000) against the native error code number. e.g. FOSSIL-1271
    could (via a simple modulo) be reduced to FOSSIL-1200 or
    FOSSIL-1000, depending on the paranoia level of the sysadmin. I have
    tried to order the result code numbers so that a dumb-down level of
    2 provides reasonably usable results without giving away too much
    detail to malicious clients.\
    (**TODO:** `g.json.errorDetailParanoia` is used to set the
    default dumb-down level, but it is currently set at compile-time.
    It needs to be moved to a config option. We have a chicken/egg scenario
    with error reporting and db access there (where the config is
    stored).)
-   Once a number is assigned to a given error condition (and actually
    used somewhere), it may not be changed/redefined. JSON clients need
    to be able to rely on stable result codes in order to provide
    adequate error reporting to their clients, and possibly for some
    error recovery logic as well (i.e. to decide whether to abort or
    retry).

The *tentative* list of result codes is shown in the following table.
These numbers/ranges are "nearly arbitrarily" chosen except for the
"special" value 0000.

**Maintenance reminder:** these codes are defined in
[`src/json_detail.h`](/finfo/src/json_detail.h) (enum
`FossilJsonCodes`) and assigned default `resultText` values in
[`src/json.c:json_err_cstr()`](/finfo/src/json.c). Changes there need
to be reflected here (and vice versa). Also, we have assertions in
place to ensure that C-side codes are in the range 1000-9999, so do
not just go blindly change the numeric ranges used by the enum.


**`FOSSIL-0###`: Non-error Category**

- `FOSSIL-0000`: Success/not an error. Succesful responses do not
  contain a resultCode, so clients should never see this.
- `FOSSIL-0###`: Reserved for potential future use in reporting
  non-fatal warnings.



**`FOSSIL-1000`: Generic Errors Category**

- `FOSSIL-1101`: Invalid request. Request envelope is invalid or
  missing.
- `FOSSIL-1102`: Unknown JSON command.
- `FOSSIL-1103`: Unknown/unspecified error
- `FOSSIL-1104`: RE-USE
- `FOSSIL-1105`: A server-side timeout was reached. (i’m not sure we
  can actually implement this one, though.)
- `FOSSIL-1106`: Assertion failed (or would have had we
  continued). Note: if an `assert()` fails in CGI/server modes, the HTTP
  response will be code 500 (Internal Server Error). We want to avoid
  that and return a JSON response instead. All of that said - there seems
  to be little reason to implement this, since assertions are "truly
  serious" errors.
- `FOSSIL-1107`: Allocation/out of memory error. This cannot be reasonably
  reported because fossil aborts if an allocation fails.
- `FOSSIL-1108`: Requested API is not yet implemented.
- `FOSSIL-1109`: Panic! Fossil's `fossil_panic()` or `cgi_panic()` was
  called. In non-JSON HTML mode this produces an HTTP 500
  error. Clients "should" report this as a potential bug, as it
  "possibly" indicates that the C code has incorrect argument- or
  error handling somewhere.
- `FOSSIL-1110`: Reading of artifact manifest failed. Time to contact
  your local fossil guru.
- `FOSSIL-1111`: Opening of file failed (e.g. POST data provided to
  CLI mode).


**`FOSSIL-2000`: Authentication/Access Error Category**

- `FOSSIL-2001`: Privileged request was missing authentication
  token/cookie.
- `FOSSIL-2002`: Access to requested resource was denied. Oftentimes
  the `resultText` property will contain a human-language description of
  the access rights needed for the given command.
- `FOSSIL-2003`: Requested command is not available in the current
  operating mode. Returned in CLI mode by commands which require HTTP
  mode (e.g. login), and vice versa. FIXME: now that we can simulate
  POST in CLI mode, we can get rid of this distinction for some of the
  commands.
- `FOSSIL-2100`: Login Failed.
- `FOSSIL-2101`: Anonymous login attempt is missing the
  "anonymousSeed" property (fetched via [the `/json/anonymousPassword`
  request](api-auth.md#login-anonymous)). Note that this is more
  specific form of `FOSSIL-3002`.


ONLY FOR TESTING purposes should the remaning 210X sub-codes be
enabled (they are potentially security-relevant, in that the client
knows which part of the request was valid/invalid):

- `FOSSIL-2102`: Name not supplied in login request
- `FOSSIL-2103`: Password not supplied in login request
- `FOSSIL-2104`: No name/password match found


**`FOSSIL-3000`: Usage Error Category**

- `FOSSIL-3001`: Invalid argument/parameter type(s) or value(s) in
  request
- `FOSSIL-3002`: Required argument(s)/parameter(s) missing from
  request
- `FOSSIL-3003`: Requested resource identifier is ambiguous (e.g. a
  shortened hash that matches multiple artifacts, an abbreviated
  date that matches multiple commits, etc.)
- `FOSSIL-3004`: Unresolved resource identifier. A branch/tag/uuid
  provided by client code could not be resolved. This is a special
  case of #3006.
- `FOSSIL-3005`: Resource already exists and overwriting/replacing is
  not allowed. e.g. trying to create a wiki page or user which already
  exists. FIXME? Consolidate this and resource-not-found into a
  separate category for dumb-down purposes?
- `FOSSIL-3006`: Requested resource not found. e.g artifact ID, branch
  name, etc.


**`FOSSIL-4000`: Database-related Error Category**

- `FOSSIL-4001`: Statement preparation failed.
- `FOSSIL-4002`: Parameter binding failed.
- `FOSSIL-4003`: Statement execution failed.
- `FOSSIL-4004`: Database locked (this is not used anywhere, but
  reserved for future use).

Special-case DB-related errors...

- `FOSSIL-4101`: Fossil Schema out of date (repo rebuild required).
- `FOSSIL-4102`: Fossil repo db could not be found.
- `FOSSIL-4103`: Repository db is not valid (possibly corrupt).
- `FOSSIL-4104`: Check-out not found. This is similar to FOSSIL-4102
  but indicates that a local checkout is required (but was not
  found). Note that the 4102 gets triggered earlier than this one, and
  so can appear in cases when a user might otherwise expect a 4104
  error.


Some of those error codes are of course "too detailed" for the client to
do anything with (e.g.. 4001-4004), but their intention is to make it
easier for Fossil developers to (A) track down problems and (B) support
clients who report problems. If a client reports, "I get a FOSSIL-4000,
how can I fix it?" then the developers/support personnel can't say much
unless they know if it's a 4001, 4002, 4003, 4004, or 4101 (in which
case they can probably zero in on the problem fairly quickly, since they
know which API call triggered it and they know (from the error code) the
general source of the problem).

## Why Standard/Immutable Result Codes are Important

-   They are easily internationalized (i.e. associated with non-English
    error text)
-   Clients may be able to add automatic retry strategies for certain
    problem types by examining the result code. e.g. if fossil returns a
    locking or timeout error \[it currently does no special
    timeout/locking handling, by the way\] the client could re-try,
    whereas a usage error cannot be sensibly retried with the same
    inputs.
-   The "category" structure described above allows us some degree of
    flexibility in how detailed the reported errors are reported.
-   While the string prefix "FOSSIL-" on the error codes may seem
    superfluous, it has one minor *potential* advantage on the client
    side: when managing several unrelated data sources, these error
    codes can be immediately identified (by higher-level code which may
    be ignorant of the data source) as having come from the fossil API.
    Think "ORA-111" vs. "111".
