TH1 Scripts
===========

TH1 is a very small scripting language used to help generate web-page
content in Fossil.

Origins
-------

TH1 began as a minimalist re-implementation of the Tcl scripting language.
There was a need to test the SQLite library on Symbian phones, but at that
time all of the test cases for SQLite were written in Tcl and Tcl could not
be easily compiled on the SymbianOS.  So TH1 was developed as a cut-down
version of Tcl that would facilitate running the SQLite test scripts on
SymbianOS.

Fossil was first being designed at about the same time.
Early prototypes of Fossil were written in pure Tcl.  But as the development
shifted toward the use of C-code, the need arose to have a Tcl-like
scripting language to help with code generation.  TH1 was small and
light-weight and used minimal resources and seemed ideally suited for the
task.

The name "TH1" stands for "Test Harness 1",
since its original purpose was to serve as testing harness
for SQLite.

Where TH1 Is Used In Fossil
---------------------------

  *  In the header and footer for [skins](./customskin.md)
     text within `<th1>...</th1>` is run as a TH1 script.
     ([example](/builtin/skins/default/header.txt))

  *  This display of [tickets](./bugtheory.wiki) is controlled by TH1
     scripts, so that the ticket format can be customized for each
     project.  Administrators can visit the <b>/tktsetup</b> page in
     their repositories to view and customize these scripts.
     ([example usage](./custom_ticket.wiki))

Overview Of The Tcl/TH1 Language
--------------------------------

TH1 is a string-processing language.  All values are strings.  Any numerical
operations are accomplished by converting from string to numeric, performing
the computation, then converting the result back into a string.  (This might
seem inefficient, but it is faster than people imagine, and numeric
computations do not come up very often for the kinds of work that TH1 does,
so it has never been an issue.)

A TH1 script consists of a sequence of commands.
Each command is terminated by the first *unescaped* newline or ";" character.
The text of the command (excluding the newline or semicolon terminator)
is broken into space-separated tokens.  The first token is the command
name and subsequent tokens are the arguments.  In this sense, TH1 syntax
is similar to the familiar command-line shell syntax.

A token is any sequence of characters other than whitespace and semicolons.
Text inside double-quotes is a single token even if it includes
whitespace and semicolons. Text within {...} pairs is also a
single token, which is useful because curly braces are easier to “pair”
and nest properly than double-quotes.

The nested {...} form of tokens is important because it allows TH1 commands
to have an appearance similar to C/C++.  It is important to remember, though,
that a TH1 script is really just a list of text commands, not a context-free
language with a grammar like C/C++.  This can be confusing to long-time
C/C++ programmers because TH1 does look a lot like C/C++, but the semantics
of TH1 are closer to FORTH or Lisp than they are to C.

Consider the `if` command in TH1.

    if {$current eq "dev"} {
      puts "hello"
    } else {
      puts "world"
    }

The example above is a single command.  The first token, and the name
of the command, is `if`.
The second token is `$current eq "dev"` - an expression.  (The outer {...}
are removed from each token by the command parser.)  The third token
is the `puts "hello"`, with its whitespace and newlines.  The fourth token
is `else` and the fifth and last token is `puts "world"`.

The `if` command evaluates its first argument (the second token)
as an expression, and if that expression is true, it evaluates its
second argument (the third token) as a TH1 script.
If the expression is false and the third argument is `else`, then
the fourth argument is evaluated as a TH1 expression.

So, you see, even though the example above spans five lines, it is really
just a single command.

All of this also explains the emphasis on *unescaped* characters above:
the curly braces `{ }` are string quoting characters in Tcl/TH1, not
block delimiters as in C. This is how we can have a command that extends
over multiple lines. It is also why the `else` keyword must be cuddled
up with the closing brace for the `if` clause's scriptlet. The following
is invalid Tcl/TH1:

    if {$current eq "dev"} {
      puts "hello"
    }
    else {
      puts "world"
    }

If you try to run this under either Tcl or TH1, the interpreter will
tell you that there is no `else` command, because with the newline on
the third line, you terminated the `if` command.

Occasionally in Tcl/TH1 scripts, you may need to use a backslash at the
end of a line to allow a command to extend over multiple lines without
being considered two separate commands. Here's an example from one of
Fossil's test scripts:

    return [lindex [regexp -line -inline -nocase -- \
        {^uuid:\s+([0-9A-F]{40}) } [eval [getFossilCommand \
        $repository "" info trunk]]] end]

Those backslashes allow the command to wrap nicely within a standard
terminal width while telling the interpreter to consider those three
lines as a single command.

<a id="taint"></a>Tainted And Untainted Strings
-----------------------------------------------

Beginning with Fossil version 2.26 (circa 2025), TH1 distinguishes between
"tainted" and "untainted" strings.  Tainted strings are strings that are
derived from user inputs that might contain text that is designed to subvert
the script.  Untainted strings are known to come from secure sources and
are assumed to contain no malicious content.

Beginning with Fossil version 2.26, and depending on the value of the
[vuln-report setting](/help?cmd=vuln-report), TH1 will prevent tainted
strings from being used in ways that might lead to XSS or SQL-injection
attacks.  This feature helps to ensure that XSS and SQL-injection
vulnerabilities are not *accidentally* added to Fossil when
custom TH1 scripts for headers or footers or tickets are added to a
repository.  Note that the tainted/untainted distinction in strings does
not make it impossible to introduce XSS and SQL-injections vulnerabilities
using poorly-written TH1 scripts; it just makes it more difficult and
less likely to happen by accident.  Developers must still consider the
security implications of TH1 customizations they add to Fossil, and take
appropriate precautions when writing custom TH1.  Peer review of TH1
script changes is encouraged.

In Fossil version 2.26, if the vuln-report setting is set to "block"
or "fatal", the [html](#html) and [query](#query) TH1 commands will
fail with an error if their argument is a tainted string.  This helps
to prevent XSS and SQL-injection attacks, respectively.  Note that
the default value of the vuln-report setting is "log", which allows those
commands to continue working and only writes a warning message into the
error log.  <b>Future versions of Fossil may change the default value
of the vuln-report setting to "block" or "fatal".</b>  Fossil users
with customized TH1 scripts are encouraged to audit their customizations
and fix any potential vulnerabilities soon, so as to avoid breakage
caused by future upgrades.  <b>Future versions of Fossil might also
place additional restrictions on the use of tainted strings.</b>
For example, it is likely that future versions of Fossil will disallow
using tainted strings as script, for example as the body of a "for"
loop or of a "proc".


Summary of Core TH1 Commands
----------------------------

The original Tcl language (after which TH1 is modeled) has a very rich
repertoire of commands.  TH1, as it is designed to be minimalist and
embedded has a greatly reduced command set.  The following bullets
summarize the commands available in TH1:

  *  array exists VARNAME
  *  array names VARNAME
  *  break
  *  catch SCRIPT ?VARIABLE?
  *  continue
  *  error ?STRING?
  *  expr EXPR
  *  for INIT-SCRIPT TEST-EXPR NEXT-SCRIPT BODY-SCRIPT
  *  foreach VARIABLE-LIST VALUE-LIST BODY-SCRIPT
  *  if EXPR SCRIPT (elseif EXPR SCRIPT)* ?else SCRIPT?
  *  info commands
  *  info exists VARNAME
  *  info vars
  *  lappend VARIABLE TERM ...
  *  lindex LIST INDEX
  *  list ARG ...
  *  llength LIST
  *  lsearch LIST STRING
  *  proc NAME ARG-LIST BODY-SCRIPT
  *  rename OLD NEW
  *  return ?-code CODE? ?VALUE?
  *  set VARNAME VALUE
  *  string compare STR1 STR2
  *  string first NEEDLE HAYSTACK ?START-INDEX?
  *  string index STRING INDEX
  *  string is CLASS STRING
  *  string last NEEDLE HAYSTACK ?START-INDEX?
  *  string match PATTERN STRING
  *  string length STRING
  *  string range STRING FIRST LAST
  *  string repeat STRING COUNT
  *  string trim STRING
  *  string trimleft STRING
  *  string trimright STRING
  *  unset VARNAME
  *  uplevel ?LEVEL? SCRIPT
  *  upvar ?FRAME? OTHERVAR MYVAR ?OTHERVAR MYVAR?

All of the above commands work as in the original Tcl.  Refer to the
<a href="https://www.tcl-lang.org/man/tcl/contents.htm">Tcl documentation</a>
for details.

Summary of Core TH1 Variables
-----------------------------

  *  tcl\_platform(engine) -- _This will always have the value "TH1"._
  *  tcl\_platform(platform) -- _This will have the value "windows" or "unix"._
  *  th\_stack\_trace -- _This will contain error stack information._

TH1 Extended Commands
---------------------

There are many new commands added to TH1 and used to access the special
features of Fossil.  The following is a summary of the extended commands:

  *  [anoncap](#anoncap)
  *  [anycap](#anycap)
  *  [artifact](#artifact)
  *  [builtin_request_js](#bireqjs)
  *  [capexpr](#capexpr)
  *  [captureTh1](#captureTh1)
  *  [cgiHeaderLine](#cgiHeaderLine)
  *  [checkout](#checkout)
  *  [combobox](#combobox)
  *  [copybtn](#copybtn)
  *  [date](#date)
  *  [decorate](#decorate)
  *  [defHeader](#defHeader)
  *  [dir](#dir)
  *  [enable\_output](#enable_output)
  *  [encode64](#encode64)
  *  [getParameter](#getParameter)
  *  [glob\_match](#glob_match)
  *  [globalState](#globalState)
  *  [hascap](#hascap)
  *  [hasfeature](#hasfeature)
  *  [html](#html)
  *  [htmlize](#htmlize)
  *  [http](#http)
  *  [httpize](#httpize)
  *  [insertCsrf](#insertCsrf)
  *  [linecount](#linecount)
  *  [markdown](#markdown)
  *  [nonce](#nonce)
  *  [puts](#puts)
  *  [query](#query)
  *  [randhex](#randhex)
  *  [redirect](#redirect)
  *  [regexp](#regexp)
  *  [reinitialize](#reinitialize)
  *  [render](#render)
  *  [repository](#repository)
  *  [searchable](#searchable)
  *  [setParameter](#setParameter)
  *  [setting](#setting)
  *  [stime](#stime)
  *  [styleHeader](#styleHeader)
  *  [styleFooter](#styleFooter)
  *  [styleScript](#styleScript)
  *  [submenu](#submenu)
  *  [taint](#taintCmd)
  *  [tclEval](#tclEval)
  *  [tclExpr](#tclExpr)
  *  [tclInvoke](#tclInvoke)
  *  [tclIsSafe](#tclIsSafe)
  *  [tclMakeSafe](#tclMakeSafe)
  *  [tclReady](#tclReady)
  *  [trace](#trace)
  *  [untaint](#untaintCmd)
  *  [unversioned content](#unversioned_content)
  *  [unversioned list](#unversioned_list)
  *  [utime](#utime)
  *  [verifyCsrf](#verifyCsrf)
  *  [verifyLogin](#verifyLogin)
  *  [wiki](#wiki)
  *  [wiki_assoc](#wiki_assoc)

Each of the commands above is documented by a block comment above their
implementation in the th\_main.c or th\_tcl.c source files.

All commands starting with "tcl", with the exception of "tclReady",
require the Tcl integration subsystem be included at compile-time.
Additionally, the "tcl" repository setting must be enabled at runtime
in order to successfully make use of these commands.

<a id="anoncap"></a>TH1 anoncap Command
-----------------------------------------

Deprecated: prefer [capexpr](#capexpr) instead.

  *  anoncap STRING...

Returns true if the anonymous user has all of the capabilities listed
in STRING.

<a id="anycap"></a>TH1 anycap Command
---------------------------------------

Deprecated: prefer [capexpr](#capexpr) instead.

  *  anycap STRING

Returns true if the current user user has any one of the capabilities
listed in STRING.

<a id="artifact"></a>TH1 artifact Command
-------------------------------------------

  *  artifact ID ?FILENAME?

Attempts to locate the specified artifact and return its contents.  An
error is generated if the repository is not open or the artifact cannot
be found.


<a id="bireqjs"></a>TH1 builtin_request_js Command
--------------------------------------------------

  *  builtin_request_js NAME

NAME must be the name of one of the
[built-in javascript source files](/dir?ci=trunk&type=flat&name=src&re=js$).
This command causes that javascript file to be appended to the delivered
document.



<a id="capexpr"></a>TH1 capexpr Command
-----------------------------------------------------

  *  capexpr CAPABILITY-EXPR

The capability expression is a list. Each term of the list is a
cluster of [capability letters](./caps/ref.html).
The overall expression is true if any
one term is true. A single term is true if all letters within that
term are true. Or, if the term begins with "!", then the term is true
if none of the terms are true. Or, if the term begins with "@" then
the term is true if all of the capability letters in that term are
available to the "anonymous" user. Or, if the term is "*" then it is
always true.

Examples:

```
capexpr {j o r}               True if any one of j, o, or r are available
capexpr {oh}                  True if both o and h are available
capexpr {@2 @3 4 5 6}         2 or 3 available for anonymous or one of
                              4, 5 or 6 is available for the user
capexpr L                     True if the user is logged in
capexpr !L                    True if the user is not logged in
```

The `L` pseudo-capability is intended only to be used on its own or with
the `!` prefix for implementing login/logout menus via the `mainmenu`
site configuration option:

```
Login     /login        !L  {}
Logout    /logout        L  {}
```

i.e. if the user is logged in, show the "Logout" link, else show the
"Login" link.

<a id="captureTh1"></a>TH1 captureTh1 Command
-----------------------------------------------------

  *  captureTh1 STRING

Executes its single argument as TH1 code and captures any
TH1-generated output as a string, which becomes the result of the
function call. e.g. any `puts` calls made from that block will not
generate any output, and instead their output will become part of the
result string.


<a id="cgiHeaderLine"></a>TH1 cgiHeaderLine Command
-----------------------------------------------------

  *  cgiHeaderLine line

Adds the specified line to the CGI header.

<a id="checkout"></a>TH1 checkout Command
-------------------------------------------

  *  checkout ?BOOLEAN?

Return the fully qualified directory name of the current checkout or an
empty string if it is not available.  Optionally, it will attempt to find
the current checkout, opening the configuration ("user") database and the
repository as necessary, if the boolean argument is non-zero.

<a id="combobox"></a>TH1 combobox Command
-------------------------------------------

  *  combobox NAME TEXT-LIST NUMLINES

Generates and emits an HTML combobox.  NAME is both the name of the
CGI parameter and the name of a variable that contains the currently
selected value.  TEXT-LIST is a list of possible values for the
combobox.  NUMLINES is 1 for a true combobox.  If NUMLINES is greater
than one then the display is a listbox with the number of lines given.

<a id="copybtn"></a>TH1 copybtn Command
-----------------------------------------

  *  copybtn TARGETID FLIPPED TEXT ?COPYLENGTH?

Output TEXT with a click-to-copy button next to it. Loads the copybtn.js
Javascript module, and generates HTML elements with the following IDs:

  *  TARGETID:       The `<span>` wrapper around TEXT.
  *  copy-TARGETID:  The `<span>` for the copy button.

If the FLIPPED argument is non-zero, the copy button is displayed after TEXT.

The optional COPYLENGTH argument defines the length of the substring of TEXT
copied to clipboard:

  *  <= 0:   No limit (default if the argument is omitted).
  *  >= 3:   Truncate TEXT after COPYLENGTH (single-byte) characters.
  *     1:   Use the "hash-digits" setting as the limit.
  *     2:   Use the length appropriate for URLs as the limit (defined at
             compile-time by `FOSSIL_HASH_DIGITS_URL`, defaults to 16).

<a id="date"></a>TH1 date Command
-----------------------------------

  *  date ?-local?

Return a string which is the current time and date.  If the -local
option is used, the date appears using localtime instead of UTC.

<a id="decorate"></a>TH1 decorate Command
-------------------------------------------

  *  decorate STRING

Renders STRING as wiki content; however, only links are handled.  No
other markup is processed.

<a id="defHeader"></a>TH1 defHeader Command
---------------------------------------------

  *  defHeader

Returns the default page header.

<a id="dir"></a>TH1 dir Command
---------------------------------

  * dir CHECKIN ?GLOB? ?DETAILS?

Returns a list containing all files in CHECKIN. If GLOB is given only
the files matching the pattern GLOB within CHECKIN will be returned.
If DETAILS is non-zero, the result will be a list-of-lists, with each
element containing at least three elements: the file name, the file
size (in bytes), and the file last modification time (relative to the
time zone configured for the repository).

<a id="enable_output"></a>TH1 enable\_output Command
------------------------------------------------------

  *  enable\_output BOOLEAN

Enable or disable sending output when the combobox, copybtn, puts, or wiki
commands are used.

<a id="encode64"></a>TH1 encode64 Command
-------------------------------------------

  *  encode64 STRING

Encode the specified string using Base64 and return the result.

<a id="getParameter"></a>TH1 getParameter Command
---------------------------------------------------

  *  getParameter NAME ?DEFAULT?

Returns the value of the specified query parameter or the specified
default value when there is no matching query parameter.

<a id="glob_match"></a>TH1 glob\_match Command
------------------------------------------------

  *  glob\_match ?-one? ?--? patternList string

Checks the string against the specified glob pattern -OR- list of glob
patterns and returns non-zero if there is a match.

<a id="globalState"></a>TH1 globalState Command
-------------------------------------------------

  *  globalState NAME ?DEFAULT?

Returns a string containing the value of the specified global state
variable -OR- the specified default value.  The supported items are:

  1. **checkout** -- _Active local checkout directory, if any._
  1. **configuration** -- _Active configuration database file name, if any._
  1. **executable** -- _Fully qualified executable file name._
  1. **flags** -- _TH1 initialization flags._
  1. **log** -- _Error log file name, if any._
  1. **repository** -- _Active local repository file name, if any._
  1. **top** -- _Base path for the active server instance, if applicable._
  1. **user** -- _Active user name, if any._
  1. **vfs** -- _SQLite VFS in use, if overridden._

Attempts to query for unsupported global state variables will result
in a script error.  Additional global state variables may be exposed
in the future.

<a id="hascap"></a>TH1 hascap Command
---------------------------------------

Deprecated: prefer [capexpr](#capexpr) instead.

  *  hascap STRING...

Returns true if the current user has all of the capabilities listed
in STRING.

<a id="hasfeature"></a>TH1 hasfeature Command
-----------------------------------------------

  *  hasfeature STRING

Returns true if the binary has the given compile-time feature enabled.
The possible features are:

  1. **ssl** -- _Support for the HTTPS transport._
  1. **legacyMvRm** -- _Support for legacy mv/rm command behavior._
  1. **execRelPaths** -- _Use relative paths with external diff/gdiff._
  1. **th1Docs** -- _Support for TH1 in embedded documentation._
  1. **th1Hooks** -- _Support for TH1 command and web page hooks._
  1. **tcl** -- _Support for Tcl integration._
  1. **useTclStubs** -- _Tcl stubs enabled in the Tcl headers._
  1. **tclStubs** -- _Uses Tcl stubs (i.e. linking with stubs library)._
  1. **tclPrivateStubs** -- _Uses Tcl private stubs (i.e. header-only)._
  1. **json** -- _Support for the JSON APIs._
  1. **markdown** -- _Support for Markdown documentation format._
  1. **unicodeCmdLine** -- _The command line arguments are Unicode._
  1. **dynamicBuild** -- _Dynamically linked to libraries._
  1. **mman** -- _Uses POSIX memory APIs from "sys/mman.h"._
  1. **see** -- _Uses the SQLite Encryption Extension._

Specifying an unknown feature will return a value of false, it will not
raise a script error.

<a id="html"></a>TH1 html Command
-----------------------------------

  *  html STRING

Outputs the STRING literally.  It is assumed that STRING contains
valid HTML, or that if STRING contains any characters that are
significant to HTML (such as `<`, `>`, `'`, or `&`) have already
been escaped, perhaps by the [htmlize](#htmlize) command.  Use the
[puts](#puts) command to output text that might contain unescaped
HTML markup.

**Beware of XSS attacks!**  If the STRING value to the html command
can be controlled by a hostile user, then he might be able to sneak
in malicious HTML or Javascript which could result in a
cross-site scripting (XSS) attack.  Be careful that all text that
in STRING that might come from user input has been sanitized by the
[htmlize](#htmlize) command or similar.  In recent versions of Fossil,
the STRING value must be [untainted](#taint) or else the "html" command 
will fail.

<a id="htmlize"></a>TH1 htmlize Command
-----------------------------------------

  *  htmlize STRING

Escape all characters of STRING which have special meaning in HTML.
Returns the escaped string.

<a id="http"></a>TH1 http Command
-----------------------------------

  *  http ?-asynchronous? ?--? url ?payload?

Performs an HTTP or HTTPS request for the specified URL.  If a
payload is present, it will be interpreted as text/plain and
the POST method will be used; otherwise, the GET method will
be used.  Upon success, if the -asynchronous option is used, an
empty string is returned as the result; otherwise, the response
from the server is returned as the result.  Synchronous requests
are not currently implemented.

<a id="httpize"></a>TH1 httpize Command
-----------------------------------------

  *  httpize STRING

Escape all characters of STRING which have special meaning in URI
components.  Returns the escaped string.

<a id="insertCsrf"></a>TH1 insertCsrf Command
-----------------------------------------------

  *  insertCsrf

While rendering a form, call this command to add the Anti-CSRF token
as a hidden element of the form.

<a id="linecount"></a>TH1 linecount Command
---------------------------------------------

  *  linecount STRING MAX MIN

Returns one more than the number of `\n` characters in STRING.  But
never returns less than MIN or more than MAX.

<a id="markdown"></a>TH1 markdown Command
-------------------------------------------

  *  markdown STRING

Renders the input string as markdown.  The result is a two-element list.
The first element contains the body, rendered as HTML.  The second element
is the text-only title string.

<a id="nonce"></a>TH1 nonce Command
-------------------------------------

  *  nonce

Returns the value of the cryptographic nonce for the request being processed.

<a id="puts"></a>TH1 puts Command
-----------------------------------

  *  puts STRING

Outputs STRING.  Characters within STRING that have special meaning
in HTML are escaped prior to being output.  Thus is it safe for STRING
to be derived from user inputs.  See also the [html](#html) command
which behaves similarly except does not escape HTML markup.  This
command ("puts") is safe to use on [tainted strings](#taint), but the "html"
command is not.

<a id="query"></a>TH1 query Command
-------------------------------------

  *  query ?-nocomplain? SQL CODE

Runs the SQL query given by the SQL argument.  For each row in the result
set, run CODE.

In SQL, parameters such as $var are filled in using the value of variable
"var".  Result values are stored in variables with the column name prior
to each invocation of CODE.  The names of the variables in which results
are stored can be controlled using "AS name" clauses in the SQL.  As
the database will often contain content that originates from untrusted
users, all result values are marked as [tainted](#taint).

**Beware of SQL injections in the `query` command!**
The SQL argument to the query command should always be literal SQL
text enclosed in {...}.  The SQL argument should never be a double-quoted
string or the value of a \$variable, as those constructs can lead to
an SQL Injection attack.  If you need to include the values of one or
more TH1 variables as part of the SQL, then put \$variable inside the
{...}.  The \$variable keyword will then get passed down into the SQLite
parser which knows to look up the value of \$variable in the TH1 symbol
table.  For example:

~~~
   query {SELECT res FROM tab1 WHERE key=$mykey} {...}
~~~

SQLite will see the \$mykey token in the SQL and will know to resolve it
to the value of the "mykey" TH1 variable, safely and without the possibility
of SQL injection.  The following is unsafe:

~~~
   query "SELECT res FROM tab1 WHERE key='$mykey'" {...}  ;# <-- UNSAFE!
~~~

In this second example, TH1 does the expansion of `$mykey` prior to passing
the text down into SQLite.  So if `$mykey` contains a single-quote character,
followed by additional hostile text, that will result in an SQL injection.

To help guard against SQL-injections, recent versions of Fossil require
that the SQL argument be [untainted](#taint) or else the "query" command
will fail.

<a id="randhex"></a>TH1 randhex Command
-----------------------------------------

  *  randhex N

Returns a string of N*2 random hexadecimal digits with N<50.  If N is
omitted, use a value of 10.

<a id="redirect"></a>TH1 redirect Command
-------------------------------------------

  *  redirect URL ?withMethod?

Issues an HTTP redirect to the specified URL and then exits the process.
By default, an HTTP status code of 302 is used.  If the optional withMethod
argument is present and non-zero, an HTTP status code of 307 is used, which
should force the user agent to preserve the original method for the request
(e.g. GET, POST) instead of (possibly) forcing the user agent to change the
method to GET.

<a id="regexp"></a>TH1 regexp Command
---------------------------------------

  *  regexp ?-nocase? ?--? exp string

Checks the string against the specified regular expression and returns
non-zero if it matches.  If the regular expression is invalid or cannot
be compiled, an error will be generated.

See [fossil grep](./grep.md) for details on the regexp syntax.

<a id="reinitialize"></a>TH1 reinitialize Command
---------------------------------------------------

  *  reinitialize ?FLAGS?

Reinitializes the TH1 interpreter using the specified flags.

<a id="render"></a>TH1 render Command
---------------------------------------

  *  render STRING

Renders the TH1 template and writes the results.

<a id="repository"></a>TH1 repository Command
-----------------------------------------------

  *  repository ?BOOLEAN?

Returns the fully qualified file name of the open repository or an empty
string if one is not currently open.  Optionally, it will attempt to open
the repository if the boolean argument is non-zero.

<a id="searchable"></a>TH1 searchable Command
-----------------------------------------------

  *  searchable STRING...

Return true if searching in any of the document classes identified
by STRING is enabled for the repository and user has the necessary
capabilities to perform the search.  The possible document classes
are:

  1. **c** -- _Check-in comments_
  1. **d** -- _Embedded documentation_
  1. **t** -- _Tickets_
  1. **w** -- _Wiki_

To be clear, only one of the document classes identified by each STRING
needs to be searchable in order for that argument to be true.  But all
arguments must be true for this routine to return true.  Hence, to see
if ALL document classes are searchable:

    if {[searchable c d t w]} {...}

But to see if ANY document class is searchable:

    if {[searchable cdtw]} {...}

This command is useful for enabling or disabling a "Search" entry on the
menu bar.

<a id="setParameter"></a>TH1 setParameter Command
---------------------------------------------------

  *  setParameter NAME VALUE

Sets the value of the specified query parameter.

<a id="setting"></a>TH1 setting Command
-----------------------------------------

  *  setting name

Gets and returns the value of the specified setting.

<a id="stime"></a>TH1 stime Command
-------------------------------------

  *  stime

Returns the number of microseconds of CPU time consumed by the current
process in system space.

<a id="styleHeader"></a>TH1 styleHeader Command
-------------------------------------------------

  *  styleHeader TITLE

Render the configured style header for the selected skin.

<a id="styleFooter"></a>TH1 styleFooter Command
-------------------------------------------------

  *  styleFooter

Render the configured style footer for the selected skin.

<a id="styleScript"></a>TH1 styleScript Command
-------------------------------------------------

  *  styleScript

Render the configured JavaScript for the selected skin.

<a id="submenu"></a>TH1 submenu Command
-----------------------------------------

  *  submenu link LABEL URL

Add hyperlink to the submenu of the current page.

<a id="taintCmd"></a>TH1 taint Command
-----------------------------------------

  *  taint STRING

This command returns a copy of STRING that has been marked as
[tainted](#taint).  Tainted strings are strings which might be
controlled by an attacker and might contain hostile inputs and
are thus unsafe to use in certain contexts.  For example, tainted
strings should not be output as part of a webpage as they might
contain rogue HTML or Javascript that could lead to an XSS
vulnerability.  Similarly, tainted strings should not be run as
SQL since they might contain an SQL-injection vulerability.

<a id="tclEval"></a>TH1 tclEval Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  tclEval arg ?arg ...?

Evaluates the Tcl script and returns its result verbatim.  If a Tcl script
error is generated, it will be transformed into a TH1 script error.  The
Tcl interpreter will be created automatically if it has not been already.

<a id="tclExpr"></a>TH1 tclExpr Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  tclExpr arg ?arg ...?

Evaluates the Tcl expression and returns its result verbatim.  If a Tcl
script error is generated, it will be transformed into a TH1 script
error.  The Tcl interpreter will be created automatically if it has not
been already.

<a id="tclInvoke"></a>TH1 tclInvoke Command
---------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclInvoke command ?arg ...?

Invokes the Tcl command using the supplied arguments.  No additional
substitutions are performed on the arguments.  The Tcl interpreter
will be created automatically if it has not been already.

<a id="tclIsSafe"></a>TH1 tclIsSafe Command
---------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclIsSafe

Returns non-zero if the Tcl interpreter is "safe".  The Tcl interpreter
will be created automatically if it has not been already.

<a id="tclMakeSafe"></a>TH1 tclMakeSafe Command
-------------------------------------------------

**This command requires the Tcl integration feature.**

  *  tclMakeSafe

Forces the Tcl interpreter into "safe" mode by removing all "unsafe"
commands and variables.  This operation cannot be undone.  The Tcl
interpreter will remain "safe" until the process terminates.  The Tcl
interpreter will be created automatically if it has not been already.

<a id="tclReady"></a>TH1 tclReady Command
-------------------------------------------

  *  tclReady

Returns true if the binary has the Tcl integration feature enabled and it
is currently available for use by TH1 scripts.

<a id="trace"></a>TH1 trace Command
-------------------------------------

  *  trace STRING

Generates a TH1 trace message if TH1 tracing is enabled.

<a id="untaintCmd"></a>TH1 taint Command
-----------------------------------------

  *  untaint STRING

This command returns a copy of STRING that has been marked as
[untainted](#taint).  Untainted strings are strings which are
believed to be free of potentially hostile content.  Use this
command with caution, as it overwrites the tainted-string protection
mechanisms that are built into TH1.  If you do not understand all
the implications of executing this command, then do not use it.

<a id="unversioned_content"></a>TH1 unversioned content Command
-----------------------------------------------------------------

  *  unversioned content FILENAME

Attempts to locate the specified unversioned file and return its contents.
An error is generated if the repository is not open or the unversioned file
cannot be found.

<a id="unversioned_list"></a>TH1 unversioned list Command
-----------------------------------------------------------

  *  unversioned list

Returns a list of the names of all unversioned files held in the local
repository.  An error is generated if the repository is not open.

<a id="utime"></a>TH1 utime Command
-------------------------------------

  *  utime

Returns the number of microseconds of CPU time consumed by the current
process in user space.

<a id="verifyCsrf"></a>TH1 verifyCsrf Command
-----------------------------------------------

  *  verifyCsrf

Before using the results of a form, first call this command to verify
that the Anti-CSRF token is present and is valid.  If the Anti-CSRF token
is missing or is incorrect, that indicates a cross-site scripting attack.
If the event of an attack is detected, an error message is generated and
all further processing is aborted.

<a id="verifyLogin"></a>TH1 verifyLogin Command
-------------------------------------------------

  *  verifyLogin

Returns non-zero if the specified user name and password represent a
valid login for the repository.

<a id="wiki"></a>TH1 wiki Command
-----------------------------------

  *  wiki STRING

Renders STRING as wiki content.

<a id="wiki_assoc"></a>TH1 wiki_assoc Command
-----------------------------------

  *  wiki_assoc STRING STRING

Renders the special wiki. The first string refers to the namespace
(checkin, branch, tag, ticket). The second string specifies the
concrete wiki page to be rendered.

Tcl Integration Commands
------------------------

When the Tcl integration subsystem is enabled, several commands are added
to the Tcl interpreter.  They are used to allow Tcl scripts access to the
Fossil functionality provided via TH1.  The following is a summary of the
Tcl commands:

  *  th1Eval
  *  th1Expr

<a id="th1Eval"></a>Tcl th1Eval Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  th1Eval arg

Evaluates the TH1 script and returns its result verbatim.  If a TH1 script
error is generated, it will be transformed into a Tcl script error.

<a id="th1Expr"></a>Tcl th1Expr Command
-----------------------------------------

**This command requires the Tcl integration feature.**

  *  th1Expr arg

Evaluates the TH1 expression and returns its result verbatim.  If a TH1
script error is generated, it will be transformed into a Tcl script error.
